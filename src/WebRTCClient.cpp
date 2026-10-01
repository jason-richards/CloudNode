#include "WebRTCClient.hpp"
#include "LocalDescription.hpp"
#include "MimeDetector.hpp"

#include "Logger.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rtc/rtc.hpp"

#include <chrono>

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
 * @brief Creates and sends the client's initial file offer.
 *
 * Builds a peer connection using the configured STUN server, gathers an
 * initial local description, obtains file metadata, and sends a JSON
 * `file_offer` message containing the room, recipient, metadata, description,
 * and ICE candidates. The recipient identifier must match the name with which
 * that peer connected to the message server.
 *
 * @param room Room containing the sender and recipient.
 * @param peer Identifier of the intended recipient.
 * @param fileDetails Path or file specification passed to MimeDetector.
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

    rtc::Configuration config;
    std::string stunServer;

    auto stunAddress = Properties().Get<std::string>("stunAddress");
    auto stunPort    = Properties().Get<int>("stunPort");
    if (stunAddress.substr(0, 5).compare("stun:") != 0) {
        stunServer = "stun:";
    }

    stunServer += stunAddress + ":" + std::to_string(stunPort);
    config.iceServers.emplace_back(stunServer);

    Logger::info() << stunServer;
	auto pc = std::make_shared<rtc::PeerConnection>(config);
    LocalDescription desc;

    if (obtainInitialDescription(pc, desc)) {
        Logger::info() << desc;
    } else {
        Logger::warn() << "Failed to get initial description.";
    }

/*
    payload = {                                                                                                         
        "type"      : "file_offer",                                                                                     
        "room"      : room,                                                                                             
        "to"        : who,                                                                                              
        "file"      : {                                                                                                 
            "name"      : filename,                                                                                     
            "size"      : sz,                                                                                           
            "mimeType"  : mimetypes.types_map[ext]                                                                      
        }                                                                                                               
    }                                                                                                                   
*/

    MimeDetector md;
    auto fdsc = md.get_file_info(fileDetails);


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
        Logger::error() << "Unable to send join request for room '" << room << "'.";
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
    (void) doc;
    Logger::info() << "FileAccept";

	//pc->onStateChange(
	//    [](rtc::PeerConnection::State state) { std::cout << "State: " << state << std::endl; });

	//pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
	//	std::cout << "Gathering State: " << state << std::endl;
	//});

	//pc->onLocalDescription([wws, id](rtc::Description description) {
	//	json message = {{"id", id},
	//	                {"type", description.typeString()},
	//	                {"description", std::string(description)}};

	//	if (auto ws = wws.lock())
	//		ws->send(message.dump());
	//});
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
    (void) doc;
    Logger::info() << "FileOffer";
    rtc::Configuration config;
    std::string stunServer;

    auto stunAddress = Properties().Get<std::string>("stunAddress");
    auto stunPort    = Properties().Get<int>("stunPort");
    if (stunAddress.substr(0, 5).compare("stun:") != 0) {
        stunServer = "stun:";
    }

    stunServer += stunAddress + ":" + std::to_string(stunPort);
    config.iceServers.emplace_back(stunServer);

    Logger::info() << stunServer;
	auto pc = std::make_shared<rtc::PeerConnection>(config);
    LocalDescription desc;

    if (obtainInitialDescription(pc, desc)) {
        Logger::info() << desc;
    } else {
        Logger::warn() << "Failed to get initial description.";
    }
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
