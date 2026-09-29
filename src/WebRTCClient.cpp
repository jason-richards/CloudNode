#include "WebRTCClient.hpp"
#include "LocalDescription.hpp"
#include "MimeDetector.hpp"

#include "Logger.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rtc/rtc.hpp"


/**
 * @file WebRTCClient.cpp
 * @brief Client-side WebRTC messaging implementation.
 *
 * This file contains the logic used by a WebRTC client to connect to a message
 * relay, join a room, send keepalive pings, and handle server-side protocol
 * notifications such as join, leave, file-transfer events, and ping responses.
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
 */
void
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
        return;
    }

    Logger::info()
        << "Requested to join room \'"
        << room
        << "\'.";
}


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
 * connection remains alive.
 */
void
WebRTCClient::SendPing() {
    if (!SendMessage("{ \"type\" : \"ping\" }")) {
        Logger::error() << "Unable to send ping.";
    }
}


/**
 * @brief Handles a server join event.
 *
 * @param doc JSON payload received for the join event.
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

    Join(peer, room);
}

/**
 * @brief Handles a server leave event.
 *
 * @param doc JSON payload received for the leave event.
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
 * @param doc JSON payload containing the offered file metadata.
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
 * @param doc JSON payload returned by the remote peer or relay.
 */
void
WebRTCClient::OnPingPong(
    rapidjson::Document& doc
) {
    (void) doc;
    Logger::info() << "PingPong";
}


/**
 * @brief Called when the underlying message transport has opened.
 *
 * On connection establishment, the client immediately joins the configured room
 * defined in the property bag using the "r" entry.
 *
 * @param peer Identifier or metadata supplied by the transport.
 */
void
WebRTCClient::OnMessageOpen(
    const std::string& peer
) {
    (void) peer;
    Logger::info() << "MessagePort Opened.";
    try {
        auto room = Properties().Get<std::string>("room");
        auto peer = Properties().Get<std::string>("peer");
        auto file = Properties().Get<std::string>("file");
        if (room.length()) {
            JoinRoom(room);

            if (file.length() && peer.length()) {
                sleep(5);
                SendFileOffer(room, peer, file);
                return;
            }
        }
    } catch (const std::exception& e) {}

    Logger::warn() << "No \'room\' to join specified.";
}

/**
 * @brief Handles closure of the underlying message transport.
 *
 * This callback logs the disconnect event and preserves the client lifecycle
 * semantics for the WebRTC message channel.
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
}
