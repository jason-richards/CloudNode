#include "WebRTCClient.hpp"
#include "LocalDescription.hpp"
#include "MimeDetector.hpp"
#include "Logger.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rtc/rtc.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <vector>

std::shared_ptr<rtc::PeerConnection>
WebRTCClient::FindPeerConnection(
    const std::string& peer,
    const std::string& room
) const {
    std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
    auto it = m_dataChannels.find({peer, room});
    return it == m_dataChannels.end() ? nullptr : it->second.pc;
}

/**
 * @file WebRTCClient.cpp
 * @brief Client-side WebRTC messaging implementation.
 *
 * This file contains the logic used by a WebRTC client to connect to a message
 * relay, join a room, send keepalive pings, and handle server-side protocol
 * notifications such as join, leave, file-transfer events, and ping responses.
 *
 * The client has a background worker that coordinates connection readiness,
 * room joining, an optional initial file offer, and periodic ping/pong checks.
 * The worker's state and configuration are protected by the client-state mutex;
 * transport callbacks wake it through the associated condition variable.
 */

std::shared_ptr<rtc::PeerConnection>
WebRTCClient::MakePeerConnection(
    const std::string& peer,
    const std::string& room
) {
    Logger::info()
        << "MakePeerConnection peer=\'"
        << peer
        << "\', room=\'"
        << room
        << "\'.";

    rtc::Configuration config;
    const auto maxMessageSize = static_cast<size_t>(Properties().Get<int>("maxMessageSize"));
    config.maxMessageSize = maxMessageSize;

    auto stunAddress = Properties().Get<std::string>("stunAddress");
    auto stunPort = Properties().Get<int>("stunPort");
    std::string stunServer;
    if (stunAddress.substr(0, 5).compare("stun:") != 0) {
        stunServer = "stun:";
    }
    stunServer += stunAddress + ":" + std::to_string(stunPort);
    config.iceServers.emplace_back(stunServer);
    Logger::info() << stunServer;

    return std::make_shared<rtc::PeerConnection>(config);
}

bool
WebRTCClient::MakeDataChannel(
    const std::string& peer,
    const std::string& room
) {
    const auto maxMessageSize = static_cast<size_t>(Properties().Get<int>("maxMessageSize"));
    DataChannel dataChannel;
    dataChannel.pc = MakePeerConnection(peer, room);
    if (!obtainInitialDescription(dataChannel, dataChannel.description)) {
        Logger::warn() << "Failed to get initial description.";
        return false;
    }

    auto channel = dataChannel.channel;
    auto fileQ = dataChannel.fileQ;
    auto fileDecision = dataChannel.fileDecision;
    channel->onOpen([this, fileQ, fileDecision, maxMessageSize, peer, room, weakChannel = rtc::weak_ptr(channel)]() {
        Logger::info() << "Offering DataChannel open!";

        auto channel = weakChannel.lock();
        if (!channel) {
            Logger::error() << "File data channel is no longer available.";
            return;
        }

        struct TransferState {
            std::mutex mutex;
            std::condition_variable changed;
            bool closed{false};
        };

        auto transfer = std::make_shared<TransferState>();

        channel->setBufferedAmountLowThreshold(maxMessageSize / 2);
        channel->onBufferedAmountLow([transfer]() {
            transfer->changed.notify_one();
        });

        channel->onClosed([transfer, fileDecision]() {
            {
                std::lock_guard<std::mutex> lock(transfer->mutex);
                transfer->closed = true;
            }
            {
                std::lock_guard<std::mutex> lock(fileDecision->mutex);
                fileDecision->closed = true;
            }
            transfer->changed.notify_one();
            fileDecision->changed.notify_all();
        });

        std::thread([this, fileQ, fileDecision, channel, transfer, maxMessageSize, peer, room]() {
            auto waitForCapacity = [&]() {
                std::unique_lock<std::mutex> lock(transfer->mutex);
                transfer->changed.wait(lock, [&]() {
                    return transfer->closed || !channel->isOpen() ||
                           channel->bufferedAmount() <= (maxMessageSize / 2);
                });

                return !transfer->closed && channel->isOpen();
            };

            while (channel->isOpen()) {
                auto queuedFilePath = GetItem(fileQ);
                if (!channel->isOpen()) {
                    return;
                }
                if (!queuedFilePath || queuedFilePath->empty()) {
                    Logger::error() << "No file path available in the data channel queue.";
                    continue;
                }

                bool fileAccepted = false;
                {
                    std::unique_lock<std::mutex> lock(fileDecision->mutex);
                    fileDecision->changed.wait(lock, [fileDecision]() {
                        return fileDecision->fileAccepted || fileDecision->fileRejected ||
                               fileDecision->closed;
                    });
                    if (fileDecision->closed || !channel->isOpen()) {
                        return;
                    }
                    fileAccepted = fileDecision->fileAccepted;
                    fileDecision->fileAccepted = false;
                    fileDecision->fileRejected = false;
                }
                if (!fileAccepted) {
                    Logger::info() << "File offer was rejected; skipping queued file.";
                    continue;
                }

                const auto& filePath = *queuedFilePath;
                std::ifstream file(filePath, std::ios::binary);
                if (!file) {
                    Logger::error() << "Unable to open file for transfer: " << filePath;
                    continue;
                }

                bool transferSucceeded = true;
                rtc::binary chunk(maxMessageSize);
                while (file.read(reinterpret_cast<char*>(chunk.data()), chunk.size()) || file.gcount() > 0) {
                    auto bytesRead = file.gcount();
                    while (channel->bufferedAmount() > maxMessageSize) {
                        if (!waitForCapacity()) {
                            Logger::warn() << "Data channel closed before file transfer completed.";
                            return;
                        }
                    }

                    Logger::info()
                        << "Sending "
                        << bytesRead;

                    if (!channel->send(chunk.data(), bytesRead)) {
                        if (!waitForCapacity()) {
                            Logger::warn() << "Data channel closed before file transfer completed.";
                            return;
                        }
                    }
                }

                if (file.bad()) {
                    Logger::error() << "Error reading file during transfer: " << filePath;
                    transferSucceeded = false;
                }

                if (transferSucceeded && channel->isOpen()) {
                    Logger::info() << "File data successfully sent: " << filePath;
                    SendFileComplete(room, peer);
                }
            }
        }).detach();
    });

    channel->onMessage([](std::variant<rtc::binary, std::string> message) {
        if (std::holds_alternative<std::string>(message)) {
            Logger::info() << "Received: " << std::get<std::string>(message);
        }
    });

    std::string payload = "{\"type\" : \"offer\", ";
    payload += "\"room\" : \"" + room + "\", ";
    payload += "\"peer\" : \"" + peer + "\", ";
    payload += dataChannel.description.GetDescriptionsJson();
    payload += ", ";
    payload += dataChannel.description.GetCandidatesJson();
    payload += "}";

    auto peerConnection = dataChannel.pc;
    {
        std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
        m_dataChannels.insert_or_assign({peer, room}, std::move(dataChannel));
    }

    if (!SendMessage(payload)) {
        Logger::error() << "Unable to send offer for peer '" << peer << "' in room '" << room << "'.";
        std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
        auto it = m_dataChannels.find({peer, room});
        if (it != m_dataChannels.end() && it->second.pc == peerConnection) {
            m_dataChannels.erase(it);
        }
        return false;
    }

    return true;
}



void
WebRTCClient::SendFileOffer(
    const std::string& room,
    const std::string& peer,
    const std::string& fileDetails
) {
    Logger::info()
        << "SendFileOffer peer=\'"
        << peer
        << "\', room=\'"
        << room
        << "\', fileDetails=\'"
        << fileDetails
        << "\'.";

    MimeDetector md;
    std::string fdsc;

    if (!md.get_file_info(fileDetails, fdsc)) {
        Logger::error()
            << "Error: "
            << fdsc;
        return;
    }

    DataChannel dataChannel;
    {
        std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
        auto it = m_dataChannels.find({peer, room});
        if (it == m_dataChannels.end() || !it->second) {
            Logger::error() << "No data channel is available for peer '" << peer << "' in room '" << room << "'.";
            return;
        }
        dataChannel = it->second;
    }
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        m_clientState = ClientState::CLIENT_WAIT_FOR_FILE_ACCEPT;
    }
    {
        std::lock_guard<std::mutex> lock(dataChannel.fileDecision->mutex);
        dataChannel.fileDecision->fileAccepted = false;
        dataChannel.fileDecision->fileRejected = false;
    }
    std::string payload = "";
    payload += "{\"type\" : \"file_offer\", ";
    payload += " \"room\" : \"" + room + "\", ";
    payload += " \"peer\" : \"" + peer + "\", ";
    payload += " \"file\" : "   + fdsc + " ";
    payload += "}";

    Logger::info() << payload;

    if (!SendMessage(payload)) {
        Logger::error() << "Unable to send file_offer command for room '" << room << "'.";
        {
            std::lock_guard<std::mutex> lock(m_clientStateMutex);
            m_clientState = ClientState::CLIENT_RUN;
        }
        m_clientStateChanged.notify_all();
        return;
    }
    AddItem(dataChannel.fileQ, std::make_shared<std::string>(fileDetails));
}



bool
WebRTCClient::SendPing() {
    if (!SendMessage("{ \"type\" : \"ping\" }")) {
        Logger::error() << "Unable to send ping.";
        return false;
    }
    return true;
}



void
WebRTCClient::SendFileComplete(
    const std::string& room,
    const std::string& peer
) {
    Logger::info()
        << "SendFileComplete: "
        << peer
        << ":"
        << room;

    const std::string payload = "{\"type\" : \"file_complete\", \"room\" : \"" + room + "\", \"peer\" : \"" + peer + "\"}";

    if (!SendMessage(payload)) {
        Logger::error() << "Unable to send file completion.";
    }
}



void
WebRTCClient::OnJoin(
    rapidjson::Document& doc
) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    Logger::info() << "OnJoin: " << buffer.GetString();

    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "Join command with no room specification.";
        return;
    }

    if (!doc.HasMember("peerId") || !doc["peerId"].IsString()) {
        Logger::warn() << "Join command with no peerId specification.";
        return;
    }

    if (!doc.HasMember("peers") || !doc["peers"].IsArray()) {
        Logger::warn() << "Join command with no peers list.";
        return;
    }

    std::string room = doc["room"].GetString();
    std::string peer = doc["peerId"].GetString();
    const std::string clientName = Properties().Get<std::string>("name");

    std::set<std::string> peers;
    for (const auto& peerValue : doc["peers"].GetArray()) {
        if (!peerValue.IsString()) {
            Logger::warn() << "Join command contains an invalid peer identifier.";
            return;
        }
        peers.insert(peerValue.GetString());
    }
    m_rooms[room] = std::move(peers);

    std::lock_guard<std::mutex> lock(m_clientStateMutex);
    if (m_clientState == ClientState::CLIENT_WAITING_FOR_JOIN &&
        room == m_room && peer == clientName) {
        m_clientState = ClientState::CLIENT_JOINED;
        m_clientStateChanged.notify_all();
    }
}



void
WebRTCClient::OnLeave(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "Leave command with no room specification.";
        return;
    }

    if (!doc.HasMember("peerId") || !doc["peerId"].IsString()) {
        Logger::warn() << "Leave command with no peerId specification.";
        return;
    }

    std::string room = doc["room"].GetString();
    std::string peer = doc["peerId"].GetString();

    WebRTC::Leave(peer, room);
}



void
WebRTCClient::OnFileAccept(
    rapidjson::Document& doc
) {
    Logger::info() << "FileAccept";

    const std::string peer = doc.HasMember("peer") && doc["peer"].IsString()
        ? doc["peer"].GetString() : m_peer;
    const std::string room = doc.HasMember("room") && doc["room"].IsString()
        ? doc["room"].GetString() : m_room;
    if (peer != m_peer || room != m_room) {
        Logger::warn() << "Ignoring file acceptance for a different peer or room.";
        return;
    }

    std::shared_ptr<FileTransferDecision> decision;
    {
        std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
        auto it = m_dataChannels.find({peer, room});
        if (it == m_dataChannels.end()) {
            Logger::error() << "Received file accept with no active data channel.";
            return;
        }
        decision = it->second.fileDecision;
    }
    {
        std::lock_guard<std::mutex> lock(decision->mutex);
        decision->fileAccepted = true;
        decision->fileRejected = false;
    }
    decision->changed.notify_all();

    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        if (m_clientState == ClientState::CLIENT_WAIT_FOR_FILE_ACCEPT) {
            m_clientState = ClientState::CLIENT_RUN;
        }
    }
    m_clientStateChanged.notify_all();
}



void
WebRTCClient::OnAnswer(
    rapidjson::Document& doc
) {
    Logger::info() << "OnAnswer.";

    const std::string peer = doc.HasMember("peer") && doc["peer"].IsString()
        ? doc["peer"].GetString()
        : (doc.HasMember("peer") && doc["peer"].IsString() ? doc["peer"].GetString() : m_peer);
    const std::string room = doc.HasMember("room") && doc["room"].IsString()
        ? doc["room"].GetString() : m_room;
    auto peerConnection = FindPeerConnection(peer, room);
    if (!peerConnection) {
        Logger::error() << "Received datachannel answer command with no active peer connection.";
        return;
    }

    if (!doc.HasMember("sdp") || !doc["sdp"].IsString()) {
        Logger::error() << "Invalid answer command format";
        return;
    }

    std::string sdp = doc["sdp"].GetString();
    std::string description;
    if (!base64Decode(sdp, description)) {
        Logger::error() << "Error decoding SDP.";
        return;
    }

    peerConnection->setRemoteDescription(rtc::Description(description, "answer"));

    Logger::info() << "Successfully set SDP.";
}

void
WebRTCClient::OnFileReject(
    rapidjson::Document& doc
) {
    const std::string peer = doc.HasMember("peer") && doc["peer"].IsString()
        ? doc["peer"].GetString() : m_peer;
    const std::string room = doc.HasMember("room") && doc["room"].IsString()
        ? doc["room"].GetString() : m_room;
    if (peer != m_peer || room != m_room) {
        Logger::warn() << "Ignoring file rejection for a different peer or room.";
        return;
    }

    std::shared_ptr<FileTransferDecision> decision;
    {
        std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
        auto it = m_dataChannels.find({peer, room});
        if (it == m_dataChannels.end()) {
            Logger::warn() << "Ignoring file rejection without an active data channel.";
            return;
        }
        decision = it->second.fileDecision;
    }
    {
        std::lock_guard<std::mutex> lock(decision->mutex);
        decision->fileAccepted = false;
        decision->fileRejected = true;
    }
    decision->changed.notify_all();
    Logger::info() << "File offer rejected by '" << peer << "' in room '" << room << "'.";

    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        if (m_clientState == ClientState::CLIENT_WAIT_FOR_FILE_ACCEPT) {
            m_clientState = ClientState::CLIENT_RUN;
        }
    }
    m_clientStateChanged.notify_all();
}



void
WebRTCClient::OnCandidate(
    rapidjson::Document& doc
) {
    Logger::info() << "OnCandidate";

    const std::string peer = doc.HasMember("peer") && doc["peer"].IsString()
        ? doc["peer"].GetString()
        : (doc.HasMember("peer") && doc["peer"].IsString() ? doc["peer"].GetString() : m_peer);
    const std::string room = doc.HasMember("room") && doc["room"].IsString()
        ? doc["room"].GetString() : m_room;
    auto peerConnection = FindPeerConnection(peer, room);
    if (!peerConnection) {
        Logger::error() << "Received remote candidate without an active peer connection.";
        return;
    }

    if (!doc.HasMember("candidate") || !doc["candidate"].IsString() ||
        !doc.HasMember("mid") || !doc["mid"].IsString()) {
        Logger::error() << "Invalid candidate command format";
        return;
    }

    std::string candidate = doc["candidate"].GetString();
    std::string mid       = doc["mid"].GetString();

    Logger::info()
        << "Adding Remote Candidate: "
        << candidate
        << ", mid="
        << mid;

    peerConnection->addRemoteCandidate(rtc::Candidate(candidate, mid));
}



void
WebRTCClient::OnOffer(
    rapidjson::Document& doc
) {
    Logger::info() << "OnOffer";

    if (!doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "FileOffer is missing its sender or room.";
        return;
    }

    const std::string peer = doc["peer"].GetString();
    const std::string room = doc["room"].GetString();
    PeerRoomKey key{peer, room};
    DataChannel dataChannel;
    dataChannel.pc = MakePeerConnection(peer, room);
    dataChannel.fileQ = CreateStringQueue();
    auto peerConnection = dataChannel.pc;
    {
        std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
        m_dataChannels.insert_or_assign(key, dataChannel);
    }

    peerConnection->onDataChannel([this, key, peer, weakPeerConnection = rtc::weak_ptr(peerConnection)](std::shared_ptr<rtc::DataChannel> dc) {
        {
            std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
            auto it = m_dataChannels.find(key);
            if (it != m_dataChannels.end()) {
                it->second.channel = dc;
            }
        }

        dc->onOpen([weakPeerConnection]() {
            Logger::info() << "onOpen: Incoming file data channel opened.";

            rtc::Candidate local;
            rtc::Candidate remote;
            auto pc = weakPeerConnection.lock();
            if (pc && pc->getSelectedCandidatePair(&local, &remote)) {
                Logger::info() << "LOCAL:\n"
                               << "  address:   " << local.address().value_or("unknown") << '\n'
                               << "  port:      " << local.port().value_or(0) << '\n'
                               << "  type:      " << local.type() << '\n'
                               << "  transport: " << local.transportType() << '\n';

                Logger::info() << "REMOTE:\n"
                               << "  address:   " << remote.address().value_or("unknown") << '\n'
                               << "  port:      " << remote.port().value_or(0) << '\n'
                               << "  type:      " << remote.type() << '\n'
                               << "  transport: " << remote.transportType() << '\n';
            }

        });

        dc->onClosed([peer]() {
            Logger::info()
                << "onClosed: DataChannel peer \""
                << peer
                << "\" closed";
        });

        dc->onMessage([peer](auto data) {
            if (std::holds_alternative<std::string>(data)) {
                Logger::info()
                    << "onMessage:  Textual message peer \""
                    << peer
                    << "\" received: "
                    << std::get<std::string>(data);
            } else {
                Logger::info()
                    << "onMessage: Binary message peer \""
                    << peer
                    << "\" received, size="
                    << std::get<rtc::binary>(data).size();
            }
        });
    });

    // 2. Set up local candidate callback (trickle ICE)
    peerConnection->onLocalCandidate([this, peer, room](rtc::Candidate candidate) {
        std::ostringstream payload;
        payload << "{ \"type\" : \"candidate\", "
                << "\"peer\" : \""
                << peer
                << "\", \"room\" : \""
                << room
                << "\", \"candidate\" : \""
                << candidate.candidate()
                << "\", \"mid\" : \""
                << candidate.mid()
                << "\"}";

        if (!SendMessage(payload.str())) {
            Logger::error() << "Problem sending candidate to server";
        }
    });

    // 3. Set up local description callback (sends the generated offer)
    peerConnection->onLocalDescription([this, peer, room](rtc::Description description) {
        std::string b64desc;
        if (!base64Encode(std::string(description), b64desc)) {
            Logger::error() << "Encountered error base64 encoding description payload.";
            return;
        }

        std::ostringstream payload;
        payload << "{ \"type\" : \""
                << std::string(description.typeString())
                << "\", \"peer\" : \""
                << peer
                << "\", \"room\" : \""
                << room
                << "\", \"sdp\" : \""
                << b64desc
                << "\"}";

        Logger::info() << payload.str();
        if (!SendMessage(payload.str())) {
            Logger::error() << "Problem sending description to server";
        }
    });

    if (!doc.HasMember("descriptions") || !doc["descriptions"].IsArray()) {
        Logger::error() << "No remote description found.";
    } else {
        std::string sdp;
        if (base64Decode(doc["descriptions"][0].GetString(), sdp)) {
            Logger::info() << "Adding Remote Description: " << sdp;
            peerConnection->setRemoteDescription(rtc::Description(sdp, "offer"));
            peerConnection->createAnswer();
        } else {
            Logger::error() << "Invalid SDP.";
        }
    }

    if (!doc.HasMember("candidates") || !doc["candidates"].IsArray()) {
        Logger::error() << "No remote candidates found.";
    } else {
        std::string candidate;
        for (rapidjson::SizeType i = 0; i < doc["candidates"].Size(); i++) {
            if (base64Decode(doc["candidates"][i].GetString(), candidate)) {
                Logger::info() << "Adding Remote Candidate: " << candidate;
                peerConnection->addRemoteCandidate(rtc::Candidate(candidate, "0"));
            } else {
                Logger::error() << "Invalid Candidate.";
            }
        }
    }

    return;
}



void
WebRTCClient::OnFileComplete(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "File completion is missing its sender or room.";
        return;
    }

    Logger::info()
        << "File transfer to '" << doc["peer"].GetString()
        << "' in room '" << doc["room"].GetString() << "' completed.";
}


void
WebRTCClient::OnPingPong(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("type") || !doc["type"].IsString() ||
        std::string(doc["type"].GetString()) != "pong" ||
        !doc.HasMember("id") || !doc["id"].IsString()) {
        return;
    }

    const std::string clientName = Properties().Get<std::string>("name");
    if (doc["id"].GetString() != clientName) {
        return;
    }

    bool receivedPong = false;
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        if (m_clientState == ClientState::CLIENT_WAIT_FOR_PONG) {
            m_clientState = ClientState::CLIENT_RUN;
            receivedPong = true;
        }
    }
    if (receivedPong) {
        Logger::info() << "Pong received.";
        m_clientStateChanged.notify_all();
    }
}

void
WebRTCClient::OnFileOffer(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("room") || !doc["room"].IsString() ||
        !doc.HasMember("file") || !doc["file"].IsObject()) {
        Logger::warn() << "FileOffer is missing its sender, room, or file metadata.";
        return;
    }

    const auto& file = doc["file"];
    if (!file.HasMember("name") || !file["name"].IsString() ||
        !file.HasMember("size") || !file["size"].IsInt64() ||
        !file.HasMember("mimeType") || !file["mimeType"].IsString()) {
        Logger::warn() << "FileOffer contains invalid file metadata.";
        return;
    }

    Logger::info()
        << "Received file offer peer '" << doc["peer"].GetString()
        << "' in room '" << doc["room"].GetString()
        << "': " << file["name"].GetString()
        << " (" << file["size"].GetInt64() << " bytes, "
        << file["mimeType"].GetString() << ").";

    std::string room = doc["room"].GetString();
    std::string peer = doc["peer"].GetString();

    SendFileAccept(doc);
}

void
WebRTCClient::SendFileAccept(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("peer") || !doc["peer"].IsString()) {
        Logger::warn() << "Cannot accept a file offer without a sender.";
        return;
    }

    const std::string peer = doc["peer"].GetString();
    auto& allocator = doc.GetAllocator();
    doc["type"].SetString("file_accept", allocator);
    doc.EraseMember("peer");
    doc.EraseMember("peer");
    doc.AddMember(
        rapidjson::Value("peer", allocator),
        rapidjson::Value(peer.c_str(), allocator),
        allocator
    );

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!SendMessage(buffer.GetString())) {
        Logger::error() << "Unable to send file_accept command.";
    }
}

void
WebRTCClient::OnMessageOpen(
    const std::string& peer
) {
    (void) peer;
    Logger::info() << "MessagePort Opened.";
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        m_transportOpen = true;
    }
    m_clientStateChanged.notify_all();
}


void
WebRTCClient::OnMessageClose(
    int code,
    std::string_view message
) {
    (void) code;
    (void) message;
    Logger::info() << "MessagePort Closed.";
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        m_clientState = ClientState::CLIENT_EXIT;
    }
    m_clientStateChanged.notify_all();
    WakeFileTransferWorkers();
}



WebRTCClient::~WebRTCClient() {
    Stop();
}



bool
WebRTCClient::Start() {
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        m_clientState = ClientState::CLIENT_INIT;
        m_transportOpen = false;
    }

    if (!WebRTC::Start()) {
        return false;
    }

    m_clientThread = std::thread(&WebRTCClient::StartClient, this);
    return true;
}



void
WebRTCClient::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        m_clientState = ClientState::CLIENT_EXIT;
    }
    m_clientStateChanged.notify_all();
    WakeFileTransferWorkers();

    WebRTC::Stop();
    if (m_clientThread.joinable()) {
        m_clientThread.join();
    }
}


void
WebRTCClient::WakeFileTransferWorkers() {
    std::vector<std::shared_ptr<FileTransferDecision>> decisions;
    {
        std::lock_guard<std::mutex> lock(m_dataChannelsMutex);
        for (const auto& [key, dataChannel] : m_dataChannels) {
            (void) key;
            decisions.push_back(dataChannel.fileDecision);
        }
    }

    for (const auto& decision : decisions) {
        {
            std::lock_guard<std::mutex> lock(decision->mutex);
            decision->closed = true;
        }
        decision->changed.notify_all();
    }
}



bool
WebRTCClient::IsRunning() {
    std::lock_guard<std::mutex> lock(m_clientStateMutex);
    return m_clientState != ClientState::CLIENT_EXIT;
}



void
WebRTCClient::StartClient() {
    std::chrono::seconds timeout{Properties().Get<int>("timeout")};

    std::unique_lock<std::mutex> lock(m_clientStateMutex);

    while (m_clientState != ClientState::CLIENT_EXIT) {
        if (m_clientState == ClientState::CLIENT_INIT) {
            if (!m_clientStateChanged.wait_for(lock, timeout, [this]() {
                return m_transportOpen || m_clientState == ClientState::CLIENT_EXIT;
            })) {
                Logger::warn() << "Timed out waiting for the message transport to open.";
                m_clientState = ClientState::CLIENT_EXIT;
                continue;
            }
            if (m_clientState == ClientState::CLIENT_EXIT) {
                continue;
            }

            // STEP 1:  If a room is set, "join" Room
            if (Properties().Contains("room")) {
                m_room = Properties().Get<std::string>("room");
            }
            if (!m_room.empty()) {
                m_clientState = ClientState::CLIENT_WAITING_FOR_JOIN;
                if (!JoinRoom(m_room)) {
                    Logger::error()
                        << "Failed to join \'"
                        << m_room 
                        << "\'";
                    m_clientState = ClientState::CLIENT_EXIT;
                    continue;
                }
                if (!m_clientStateChanged.wait_for(lock, timeout, 
                    [this]() {
                        return m_clientState == ClientState::CLIENT_JOINED ||
                               m_clientState == ClientState::CLIENT_EXIT;
                    })
                ) {
                    Logger::warn() << "Timed out waiting for \"join\" to complete.";
                    m_clientState = ClientState::CLIENT_EXIT;
                    continue;
                };
                if (m_clientState == ClientState::CLIENT_EXIT) {
                    continue;
                }
            }

            // STEP 2: If a peer is set, establish a datachannel
            if (!m_room.empty() && Properties().Contains("peer")) {
                m_peer = Properties().Get<std::string>("peer");
                if (!m_peer.empty()) {
                    const auto peer = m_peer;
                    const auto room = m_room;
                    lock.unlock();
                    const bool channelCreated = MakeDataChannel(peer, room);
                    lock.lock();
                    if (m_clientState == ClientState::CLIENT_EXIT) {
                        continue;
                    }
                    if (!channelCreated) {
                        m_clientState = ClientState::CLIENT_EXIT;
                        continue;
                    }
                }
            }


            // STEP 3: If file is set, send it
            if (Properties().Contains("file")) {
                m_file = Properties().Get<std::string>("file");

                if (!m_peer.empty() && !m_file.empty()) {
                    const auto peer = m_peer;
                    const auto room = m_room;
                    const auto file = m_file;
                    lock.unlock();
                    SendFileOffer(room, peer, file);
                    lock.lock();
                    if (m_clientState == ClientState::CLIENT_EXIT) {
                        continue;
                    }
                }
            }

            if (m_clientState != ClientState::CLIENT_WAIT_FOR_FILE_ACCEPT) {
                m_clientState = ClientState::CLIENT_RUN;
            }
            continue;
        }

        if (m_clientState == ClientState::CLIENT_WAIT_FOR_FILE_ACCEPT) {
            m_clientStateChanged.wait(lock, [this]() {
                return m_clientState != ClientState::CLIENT_WAIT_FOR_FILE_ACCEPT;
            });
            continue;
        }

        if (m_clientState == ClientState::CLIENT_RUN) {
            const bool interrupted = m_clientStateChanged.wait_for(
                lock,
                timeout,
                [this]() { return m_clientState != ClientState::CLIENT_RUN; }
            );
            if (!interrupted && m_clientState == ClientState::CLIENT_RUN) {
                m_clientState = ClientState::CLIENT_PING;
            }
            continue;
        }
        if (m_clientState == ClientState::CLIENT_PING) {
            m_clientState = ClientState::CLIENT_WAIT_FOR_PONG;
            lock.unlock();
            const bool sent = SendPing();
            lock.lock();
            if (!sent && m_clientState == ClientState::CLIENT_WAIT_FOR_PONG) {
                m_clientState = ClientState::CLIENT_EXIT;
            }
            continue;
        }

        if (m_clientState == ClientState::CLIENT_WAIT_FOR_PONG) {
            const bool interrupted = m_clientStateChanged.wait_for(
                lock,
                timeout,
                [this]() { return m_clientState != ClientState::CLIENT_WAIT_FOR_PONG; }
            );
            if (!interrupted && m_clientState == ClientState::CLIENT_WAIT_FOR_PONG) {
                Logger::warn() << "Timed out waiting for pong.";
                m_clientState = ClientState::CLIENT_EXIT;
            }
            continue;
        }

        m_clientStateChanged.wait(lock, [this]() {
                 return m_clientState == ClientState::CLIENT_INIT ||
                     m_clientState == ClientState::CLIENT_JOINED ||
                     m_clientState == ClientState::CLIENT_RUN ||
                     m_clientState == ClientState::CLIENT_PING ||
                     m_clientState == ClientState::CLIENT_WAIT_FOR_PONG ||
                     m_clientState == ClientState::CLIENT_EXIT;
        });
    }
}



bool
WebRTCClient::JoinRoom(
    const std::string& room
) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();
    doc.AddMember("type", rapidjson::Value("join", allocator), allocator);
    doc.AddMember("room", rapidjson::Value(room.c_str(), allocator), allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    if (!SendMessage(buffer.GetString())) {
        Logger::error() << "Unable to send join request for room '" << room << "'.";
        return false;
    }

    Logger::info()
        << "Requested to join room \'"
        << room
        << "\'.";

    return true;
}

