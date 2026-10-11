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



/**
 * @brief Sends a room-join request to the message server.
 *
 * The request is encoded as a JSON object with the following shape:
 * @code
 * { "type": "join", "room": "<room-id>" }
 * @endcode
 *
 * @param room Identifier of the room to join.
 * @return true if the serialized join request was sent successfully.
 */
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



/**
 * @brief Builds a file-transfer offer and sends it over the signalling channel.
 *
 * The function creates a fresh peer connection, configures the STUN server, and
 * gathers the initial SDP and ICE candidates required to establish the data
 * channel. Once the local description is ready, it serializes a JSON `file_offer`
 * payload with the room, target peer, file metadata, description, and candidate
 * information and sends it to the message relay. After the channel opens, the
 * sender streams the actual file bytes over the data channel in bounded chunks.
 *
 * @param room Room containing the sender and recipient.
 * @param peer Identifier of the intended peer receiving the offer.
 * @param fileDetails Path to the file that will be transferred.
 */
void
WebRTCClient::SendFileOffer(
    const std::string&  room,
    const std::string&  peer,
    const std::string&  fileDetails
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

    rtc::Configuration config;
    const auto maxMessageSize = static_cast<size_t>(Properties().Get<int>("maxMessageSize"));
    config.maxMessageSize = maxMessageSize;

    std::string stunServer;

    auto stunAddress = Properties().Get<std::string>("stunAddress");
    auto stunPort    = Properties().Get<int>("stunPort");
    if (stunAddress.substr(0, 5).compare("stun:") != 0) {
        stunServer = "stun:";
    }

    stunServer += stunAddress + ":" + std::to_string(stunPort);
    config.iceServers.emplace_back(stunServer);

    Logger::info() << stunServer;
    m_pc = std::make_shared<rtc::PeerConnection>(config);
    LocalDescription desc;

    if (!obtainInitialDescription(m_pc, m_dc, desc)) {
        Logger::warn() << "Failed to get initial description.";
        return;
    }

    m_dc->onOpen([fileDetails, maxMessageSize, weakChannel = rtc::weak_ptr(m_dc)]() {
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

        channel->setBufferedAmountLowThreshold(maxMessageSize/2);
        channel->onBufferedAmountLow([transfer]() {
            transfer->changed.notify_one();
        });

        channel->onClosed([transfer]() {
            std::lock_guard<std::mutex> lock(transfer->mutex);
            transfer->closed = true;
            transfer->changed.notify_one();
        });

        std::thread([fileDetails, channel, transfer, maxMessageSize]() {
            std::ifstream file(fileDetails, std::ios::binary);
            if (!file) {
                Logger::error() << "Unable to open file for transfer: " << fileDetails;
                return;
            }

            auto waitForCapacity = [&]() {
                std::unique_lock<std::mutex> lock(transfer->mutex);
                transfer->changed.wait(lock,
                    [&]() {
                        return transfer->closed || !channel->isOpen() ||
                           channel->bufferedAmount() <= (maxMessageSize / 2);
                    }
                );

                return !transfer->closed && channel->isOpen();
            };

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
                    continue;
                }
            }

            if (file.bad()) {
                Logger::error() << "Error reading file during transfer: " << fileDetails;
                return;
            }

            Logger::info() << "File data sent: " << fileDetails;
        }).detach();
    });

    m_dc->onMessage([](std::variant<rtc::binary, std::string> message) {
        if (std::holds_alternative<std::string>(message)) {
            Logger::info() << "Received: " << std::get<std::string>(message);
        }
    });


    std::string payload = "";
    payload += "{\"type\" : \"file_offer\", ";
    payload += " \"room\" : \"" + room + "\", ";
    payload += " \"peer\" : \"" + peer + "\", ";
    payload += " \"file\" : "   + fdsc + ", ";  

    payload += desc.GetDescriptionsJson();
    payload += ", ";
    payload += desc.GetCandidatesJson();
    payload += "}";

    Logger::info() << payload;

    if (!SendMessage(payload)) {
        Logger::error() << "Unable to send file_offer command for room '" << room << "'.";
        return;
    }
}



/**
 * @brief Sends a lightweight keepalive ping to the message server.
 *
 * The ping payload is intentionally minimal and is used to verify that the
 * connection remains alive. A successful send only confirms that the local
 * transport accepted the message; the worker waits separately for a matching
 * pong response.
 *
 * @return true if the ping was sent successfully.
 */
bool
WebRTCClient::SendPing() {
    if (!SendMessage("{ \"type\" : \"ping\" }")) {
        Logger::error() << "Unable to send ping.";
        return false;
    }
    return true;
}



/**
 * @brief Handles a server join event.
 *
 * Adds the announced peer to this client's local room membership. When the
 * event confirms this client has joined its configured room, changes the
 * worker state to `CLIENT_JOINED` and wakes the worker.
 *
 * @param doc JSON payload containing the room and peerId fields.
 */
void
WebRTCClient::OnJoin(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "Join command with no room specification.";
        return;
    }

    if (!doc.HasMember("peerId") || !doc["peerId"].IsString()) {
        Logger::warn() << "Join command with no peerId specification.";
        return;
    }

    std::string room = doc["room"].GetString();
    std::string peer = doc["peerId"].GetString();
    const std::string clientName = Properties().Get<std::string>("name");

    Join(peer, room);

    bool joined = false;
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        if (m_clientState == ClientState::CLIENT_WAITING_FOR_JOIN &&
            room == m_room && peer == clientName) {
            m_clientState = ClientState::CLIENT_JOINED;
            joined = true;
        }
    }
    if (joined) {
        m_clientStateChanged.notify_all();
    }
}



/**
 * @brief Handles a server leave event.
 *
 * Removes the announced peer from this client's local room membership. The
 * event is ignored if either the room or peerId field is missing or has the
 * wrong JSON type.
 *
 * @param doc JSON payload containing the room and peerId fields.
 */
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



/**
 * @brief Handles a file-accept notification.
 *
 * This handler currently logs the notification only; the document is not yet
 * used to update a peer connection or continue a file transfer.
 *
 * @param doc JSON payload describing the accepted file transfer.
 */
void
WebRTCClient::OnFileAccept(
    rapidjson::Document& doc
) {
    Logger::info() << "FileAccept";

    if (!doc.HasMember("descriptions") || !doc["descriptions"].IsArray()) {
        Logger::warn() << "File accept is missing a remote description.";
        return;
    }

    if (!m_pc) {
        Logger::error() << "Received file accept with no active peer connection.";
        return;
    }

    for (rapidjson::SizeType i = 0; i < doc["descriptions"].Size(); ++i) {
        if (!doc["descriptions"][i].IsString()) {
            continue;
        }

        std::string sdp;
        if (!base64Decode(doc["descriptions"][i].GetString(), sdp)) {
            Logger::error() << "Invalid SDP in file accept payload.";
            continue;
        }

        Logger::info() << "Applying remote answer: " << sdp;
        m_pc->setRemoteDescription(rtc::Description(sdp, "answer"));
    }

    if (doc.HasMember("candidates") && doc["candidates"].IsArray()) {
        std::string candidate;
        for (rapidjson::SizeType i = 0; i < doc["candidates"].Size(); ++i) {
            if (!doc["candidates"][i].IsString()) {
                continue;
            }

            candidate.clear();
            if (!base64Decode(doc["candidates"][i].GetString(), candidate)) {
                Logger::error() << "Invalid candidate in file accept payload.";
                continue;
            }

            Logger::info() << "Adding accepted remote candidate: " << candidate;
            m_pc->addRemoteCandidate(rtc::Candidate(candidate, "0"));
        }
    }
}



/**
 * @brief Applies a remote SDP answer for an established peer connection.
 *
 * The signalling server forwards the remote side's SDP answer as a Base64-encoded
 * string. This handler decodes that payload, validates the presence of an active
 * peer connection, and sets the remote description so the data channel can finish
 * the WebRTC handshake.
 *
 * @param doc JSON payload containing the answer SDP.
 */
void
WebRTCClient::OnAnswer(
    rapidjson::Document& doc
) {
    Logger::info() << "OnAnswer.";

    if (!m_pc) {
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

    m_pc->setRemoteDescription(rtc::Description(description, "answer"));
    Logger::info() << "Successfully set SDP.";
}



/**
 * @brief Adds a remote ICE candidate to the active peer connection.
 *
 * Trickled ICE candidates are sent over the signalling channel as Base64-encoded
 * strings and a media ID. The handler validates the fields, decodes the
 * candidate, and registers it with the current RTCPeerConnection so the WebRTC
 * negotiation can complete as ICE gathering continues.
 *
 * @param doc JSON payload containing the remote ICE candidate and MID.
 */
void
WebRTCClient::OnCandidate(
    rapidjson::Document& doc
) {
    Logger::info() << "OnCandidate";

    if (!m_pc) {
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

    m_pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
}



/**
 * @brief Handles a file-offer event.
 *
 * The current implementation initializes a peer connection with the
 * configured STUN server and gathers an initial local description. It does
 * not yet read the offer document or apply remote descriptions/candidates.
 *
 * @param doc JSON payload containing the offered file metadata and signaling
 *            data; currently unused by this implementation.
 */
void
WebRTCClient::OnFileOffer(
    rapidjson::Document& doc
) {
    Logger::info() << "OnFileOffer";

    rtc::Configuration config;
    config.maxMessageSize = static_cast<size_t>(Properties().Get<int>("maxMessageSize"));

    std::string stunServer;

    auto stunAddress = Properties().Get<std::string>("stunAddress");
    auto stunPort    = Properties().Get<int>("stunPort");
    if (stunAddress.substr(0, 5).compare("stun:") != 0) {
        stunServer = "stun:";
    }

    stunServer += stunAddress + ":" + std::to_string(stunPort);
    config.iceServers.emplace_back(stunServer);

    Logger::info() << stunServer;

    m_pc = std::make_shared<rtc::PeerConnection>(config);

    if (!doc.HasMember("from") || !doc["from"].IsString()) {
        Logger::warn() << "FileOffer with no from field.";
        return;
    }

    std::string from = doc["from"].GetString();
    m_pc->onDataChannel([this, from](std::shared_ptr<rtc::DataChannel> dc) {
        {
            std::lock_guard<std::mutex> lock(m_clientStateMutex);
            m_dc = dc;
        }

        dc->onOpen([weakPeerConnection = rtc::weak_ptr(m_pc)]() {
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

        dc->onClosed([from]() {
            Logger::info()
                << "onClosed: DataChannel from \""
                << from
                << "\" closed";
        });

        dc->onMessage([from](auto data) {
            if (std::holds_alternative<std::string>(data)) {
                Logger::info()
                    << "onMessage:  Textual message from \""
                    << from 
                    << "\" received: "
                    << std::get<std::string>(data);
            } else {
                Logger::info()
                    << "onMessage: Binary message from \""
                    << from
                    << "\" received, size="
                    << std::get<rtc::binary>(data).size();
            }
        });
    });


    // 2. Set up local candidate callback (trickle ICE)
    m_pc->onLocalCandidate([this, from](rtc::Candidate candidate) {
        std::ostringstream payload;
        payload << "{ \"type\" : \"candidate\", "
                << "\"peer\" : \""
                << from
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
    m_pc->onLocalDescription([this, from](rtc::Description description) {
        std::string b64desc;
        if (!base64Encode(std::string(description), b64desc)) {
            Logger::error() << "Encountered error base64 encoding description payload.";
            return;
        }

        std::ostringstream payload;

        payload << "{ \"type\" : \""
                << std::string(description.typeString())
                << "\", \"peer\" : \""
                << from
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
            m_pc->setRemoteDescription(rtc::Description(sdp, "offer"));
            m_pc->createAnswer();
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
                m_pc->addRemoteCandidate(rtc::Candidate(candidate, "0"));
            } else {
                Logger::error() << "Invalid Candidate.";
            }
        }
    }

    return;
}



/**
 * @brief Handles a ping/pong response from the message server.
 *
 * Ignores malformed messages, non-pong messages, and pongs addressed to a
 * different client. A matching pong received while waiting transitions the
 * worker back to `CLIENT_RUN` and wakes it.
 *
 * @param doc JSON payload returned by the message server.
 */
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


/**
 * @brief Handles the message transport's open callback.
 *
 * Marks the transport ready and wakes the worker. The worker, rather than this
 * callback, sends the configured room-join request.
 *
 * @param peer Identifier supplied by the transport; unused by this client.
 */
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



/**
 * @brief Handles closure of the underlying message transport.
 *
 * Marks the worker as exiting and wakes it so any condition-variable wait can
 * finish. The close code and reason are currently logged only indirectly and
 * are not otherwise used.
 *
 * @param code WebSocket close code.
 * @param message WebSocket close message.
 */
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
}



/**
 * @brief Stops the client when it is destroyed.
 *
 * Delegates to Stop so the transport and background worker are shut down
 * before the object is released.
 */
WebRTCClient::~WebRTCClient() {
    Stop();
}



/**
 * @brief Starts the message transport and the client worker thread.
 *
 * Resets the worker state, starts the base WebRTC transport, and launches
 * StartClient to perform the room-join and keepalive workflow. If the base
 * transport cannot start, no worker thread is launched.
 *
 * @return The result of starting the base WebRTC transport.
 */
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



/**
 * @brief Requests client shutdown and joins its worker thread.
 *
 * Sets the exit state under the state mutex, wakes any worker wait, stops the
 * base transport, and joins the worker if it was started.
 */
void
WebRTCClient::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_clientStateMutex);
        m_clientState = ClientState::CLIENT_EXIT;
    }
    m_clientStateChanged.notify_all();

    WebRTC::Stop();
    if (m_clientThread.joinable()) {
        m_clientThread.join();
    }
}



/**
 * @brief Reports whether the client worker has not entered its exit state.
 *
 * This reports the client lifecycle state, not the underlying transport's
 * independent running flag.
 *
 * @return true unless the worker state is `CLIENT_EXIT`.
 */
bool
WebRTCClient::IsRunning() {
    std::lock_guard<std::mutex> lock(m_clientStateMutex);
    return m_clientState != ClientState::CLIENT_EXIT;
}



/**
 * @brief Runs the client lifecycle and keepalive state machine.
 *
 * Reads configuration from the property bag, waits for the transport-open
 * callback, requests room membership, optionally sends the configured initial
 * file offer, then alternates between an idle interval and ping/pong checks.
 * The state mutex protects state and configuration shared with callbacks; it
 * is released around transport and file-offer operations to avoid holding it
 * during external work.
 *
 * State flow:
 * - `CLIENT_INIT` loads configuration and waits for the transport.
 * - `CLIENT_WAITING_FOR_JOIN` waits for this client's join notification.
 * - `CLIENT_JOINED` sends an initial offer when both peer and file are set.
 * - `CLIENT_RUN` waits for the timeout interval before requesting a ping.
 * - `CLIENT_PING` sends a ping and enters `CLIENT_WAIT_FOR_PONG`.
 * - `CLIENT_WAIT_FOR_PONG` returns to `CLIENT_RUN` on a matching pong, or
 *   enters `CLIENT_EXIT` on send failure or timeout.
 *
 * Invalid configuration, a failed join send, a transport close, or an
 * explicit Stop request also leads to `CLIENT_EXIT`.
 */
void
WebRTCClient::StartClient() {
    std::chrono::seconds timeout{5};
    std::unique_lock<std::mutex> lock(m_clientStateMutex);
    while (m_clientState != ClientState::CLIENT_EXIT) {
        if (m_clientState == ClientState::CLIENT_INIT) {
            lock.unlock();
            std::string room;
            std::string peer;
            std::string file;
            int timeoutSeconds;
            try {
                room = Properties().Get<std::string>("room");
                timeoutSeconds = Properties().Get<int>("timeout");
                if (Properties().Contains("peer")) {
                    peer = Properties().Get<std::string>("peer");
                }
                if (Properties().Contains("file")) {
                    file = Properties().Get<std::string>("file");
                }
            } catch (const std::exception& error) {
                Logger::error() << "Unable to read client properties: " << error.what();
                lock.lock();
                m_clientState = ClientState::CLIENT_EXIT;
                continue;
            }
            if (timeoutSeconds <= 0) {
                Logger::error() << "Client timeout must be greater than zero seconds.";
                lock.lock();
                m_clientState = ClientState::CLIENT_EXIT;
                continue;
            }
            timeout = std::chrono::seconds(timeoutSeconds);
            lock.lock();

            m_room = std::move(room);
            m_peer = std::move(peer);
            m_file = std::move(file);
            m_clientStateChanged.wait(lock, [this]() {
                return m_transportOpen || m_clientState == ClientState::CLIENT_EXIT;
            });
            if (m_clientState == ClientState::CLIENT_EXIT) {
                break;
            }

            if (m_room.empty()) {
                Logger::warn() << "No 'room' to join specified.";
                m_clientState = ClientState::CLIENT_RUN;
                continue;
            }

            m_clientState = ClientState::CLIENT_WAITING_FOR_JOIN;
            lock.unlock();
            const bool sent = JoinRoom(m_room);
            lock.lock();
            if (!sent && m_clientState != ClientState::CLIENT_EXIT) {
                m_clientState = ClientState::CLIENT_EXIT;
            }
            continue;
        }

        if (m_clientState == ClientState::CLIENT_JOINED) {
            const auto room = m_room;
            const auto peer = m_peer;
            const auto file = m_file;
            lock.unlock();
            if (!peer.empty() && !file.empty()) {
                SendFileOffer(room, peer, file);
            }
            lock.lock();
            if (m_clientState == ClientState::CLIENT_JOINED) {
                m_clientState = ClientState::CLIENT_RUN;
            }
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

