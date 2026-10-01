#include "WebRTCServer.hpp"

#include "Logger.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <algorithm>



/**
 * @brief Handles ping and pong commands by sending a pong response to the client.
 *
 * @param doc Parsed command document.
 */
void
WebRTCServer::OnPingPong(
    rapidjson::Document& doc
) {
    (void) doc;
    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }
    auto peer = ws->getUserData()->x_client_id;
    Logger::info() << "\'" << peer << "\' sent \'ping\'; sending \'pong\'.";
    ws->send("{\"type\" : \"pong\", \"id\" : \"" + peer + "\"}");
}



/**
 * @brief Notifies a client that another client joined its room.
 *
 * @param whoToNotify Identifier of the client receiving the notification.
 * @param whoJoined Identifier of the client who joined.
 */
void
WebRTCServer::SendJoinNotification(
    const std::string& whoToNotify,
    const std::string& whoJoined,
    const std::string& room
) {
        auto ws = m_clientDB.find(whoToNotify);
        if (ws == m_clientDB.end()) {
        Logger::error() << "User: " + whoToNotify + " not found.";
        return;
    }

    Logger::info() << "Notifying \'" << whoToNotify << "\' that \'" << whoJoined << "\' joined.";
    ws->second->send("{\"type\" : \"peer_joined\", \"peerId\" : \"" + whoJoined + "\", \"room\" : \"" + room + "\" }");
}



/**
 * @brief Handles a join command and adds the client to the requested room.
 *
 * @param doc Parsed command document containing the room identifier.
 */
void
WebRTCServer::OnJoin(
    rapidjson::Document& doc
) {
    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }

    auto peer = ws->getUserData()->x_client_id;
    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "Join command with no room specification.";
        return;
    }

    std::string room = doc["room"].GetString();

    Logger::info() << "User \'" << peer << "\' requested to join room \'" << room << "\'.";

    if (!Join(peer, room)) {
        return;
    }

        m_clientDB[peer] = ws;
        for (auto u : m_rooms[room]) {
        SendJoinNotification(u, peer, room);
    }
}



/**
 * @brief Notifies a client that another client left its room.
 *
 * @param whoToNotify Identifier of the client receiving the notification.
 * @param whoLeft Identifier of the client who left.
 */
void
WebRTCServer::SendLeaveNotification(
    const std::string& whoToNotify,
    const std::string& whoLeft,
    const std::string& room
) {
        auto ws = m_clientDB.find(whoToNotify);
        if (ws == m_clientDB.end()) {
        Logger::warn() << "User: " + whoToNotify + " not found.";
        return;
    }

    Logger::info() << "Notifying \'" << whoToNotify << "\' that \'" << whoLeft << "\' left.";
    ws->second->send("{\"type\" : \"peer_left\", \"peerId\" : \"" + whoLeft + "\", \"room\" : \"" + room + "\" }");
}



/**
 * @brief Removes a client from a room and notifies the remaining members.
 *
 * @param peer Identifier of the departing client.
 * @param room Room from which the client is leaving.
 */
void
WebRTCServer::Leave(
    const std::string& peer,
    const std::string& room
) {
    WebRTC::Leave(peer, room);
     for (auto u : m_rooms[room]) {
        SendLeaveNotification(u, peer, room);
    }
}



/**
 * @brief Handles a leave command received from a client.
 *
 * @param doc Parsed command document containing the room identifier.
 */
void
WebRTCServer::OnLeave(
    rapidjson::Document& doc
) {
    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }
    auto peer = ws->getUserData()->x_client_id;
    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "leave command with no room specification.";
        return;
    }

    Leave(peer, doc["room"].GetString());
}



/**
 * @brief Validates and forwards a file command to its intended recipient.
 *
 * The sender and recipient must both belong to the requested room. The
 * forwarded document replaces the recipient field with the sender identifier.
 *
 * @param doc Parsed file command document.
 * @param file File metadata extracted from the command.
 */
void
WebRTCServer::HandleFile(
    rapidjson::Document& doc,
    FileDetails& file
) {
    (void) file;
    if (!doc.HasMember("room") || !doc["room"].IsString() ||
        !doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("file") || !doc["file"].IsObject()) {
        Logger::error() << "Invalid file command format";
        return;
    }

    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }
    std::string fromId = ws->getUserData()->x_client_id;
    std::string peerId = doc["peer"].GetString();
    std::string room = doc["room"].GetString();
    if (m_rooms[room].find(fromId) == m_rooms[room].end() ||
        m_rooms[room].find(peerId) == m_rooms[room].end()
    ) {
        Logger::error() << "File command participants must both be in room \'" << room << "\'.";
        return;
    }

    doc.EraseMember("peer");
    doc.AddMember(
        rapidjson::Value("from", doc.GetAllocator()),
        rapidjson::Value(fromId.c_str(), doc.GetAllocator()),
        doc.GetAllocator());

        auto toWs = m_clientDB.find(peerId);
        if (toWs == m_clientDB.end()) {
        Logger::error() << "Unable to find socket for \'" << peerId << "\'.";
        return;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!toWs->second->send(buffer.GetString())) {
        Logger::error() << "Error encountered making offer to \'" << peerId << "\'.";
    }
}



/**
 * @brief Handles a file offer and forwards it to the requested recipient.
 *
 * @param doc Parsed file offer document.
 */
void
WebRTCServer::OnFileOffer(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("file") || !doc["file"].IsObject() ||
        !doc["file"].HasMember("name") || !doc["file"]["name"].IsString() ||
        !doc["file"].HasMember("size") || !doc["file"]["size"].IsInt64() ||
        !doc["file"].HasMember("mimeType") || !doc["file"]["mimeType"].IsString()) {
        Logger::error() << "Invalid file details in file offer command";
        return;
    }

    FileDetails file{
        doc["file"]["name"].GetString(),
        static_cast<size_t>(doc["file"]["size"].GetInt64()),
        doc["file"]["mimeType"].GetString()
    };
    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }
    Logger::info() << "File offer from \'" << ws->getUserData()->x_client_id
                   << "\', to \'" << doc["peer"].GetString() << "\': " << file.name;
    HandleFile(doc, file);
}



/**
 * @brief Handles acceptance of a file offer and forwards it to the sender.
 *
 * @param doc Parsed file acceptance document.
 */
void
WebRTCServer::OnFileAccept(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("file") || !doc["file"].IsObject() ||
        !doc["file"].HasMember("name") || !doc["file"]["name"].IsString()) {
        Logger::error() << "Invalid file details in file accept command";
        return;
    }

    FileDetails file{doc["file"]["name"].GetString(), 0, ""};
    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }
    Logger::info() << "File offer accept from \'" << ws->getUserData()->x_client_id
                   << "\', to \'" << doc["to"].GetString() << "\': " << file.name;
    HandleFile(doc, file);
}



/**
 * @brief Handles a newly opened WebSocket connection.
 *
 * @param peer Identifier associated with the connection.
 */
void
WebRTCServer::OnMessageOpen(
    const std::string& peer
) {
    Logger::info() << "\'" << peer << "\' connected.";
}



/**
 * @brief Handles a closed WebSocket connection and removes its room membership.
 *
 * @param code WebSocket close code.
 * @param message WebSocket close message.
 */
void
WebRTCServer::OnMessageClose(
    int code,
    std::string_view message
) {
    (void) code;
    (void) message;
    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }
    auto peer = ws->getUserData()->x_client_id;
    Logger::info() << "\'" << peer << "\' has closed connection.";
    for (const auto& [room, peers] : m_rooms) {
        if (peers.find(peer) != peers.end()) {
            Leave(peer, room);
        }
    }
}

