#include "WebRTCClient.hpp"
#include "Logger.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

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
    (void) doc;
    Logger::info() << "Join";
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
    (void) doc;
    Logger::info() << "Leave";
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
 * @param user Identifier or metadata supplied by the transport.
 */
void
WebRTCClient::OnMessageOpen(
    const std::string& user
) {
    (void) user;
    Logger::info() << "MessagePort Opened.";
    JoinRoom(Properties().Get<std::string>("r"));
    //SendPing();
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
