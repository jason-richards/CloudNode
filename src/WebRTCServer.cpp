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
    auto user = ws->getUserData()->x_client_id;
    Logger::info() << "\'" << user << "\' sent \'ping\'; sending \'pong\'.";
    ws->send("{\"type\" : \"pong\", \"id\" : \"" + user + "\"}");
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
    const std::string& whoJoined
) {
    auto ws = clientDB_.find(whoToNotify);
    if (ws == clientDB_.end()) {
        Logger::error() << "User: " + whoToNotify + " not found.";
        return;
    }

    Logger::info() << "Notifying \'" << whoToNotify << "\' that \'" << whoJoined << "\' joined.";
    ws->second->send("{\"type\" : \"peer_joined\", \"peerId\" : \"" + whoJoined + "\"}");
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
    auto user = ws->getUserData()->x_client_id;
    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "Join command with no room specification.";
        return;
    }

    std::string room = doc["room"].GetString();
    Logger::info() << "User \'" << user << "\' requested to join room \'" << room << "\'.";
    if (rooms_.find(room) == rooms_.end()) {
        rooms_[room] = std::vector<std::string>();
    }
    if (std::find(rooms_[room].begin(), rooms_[room].end(), user) != rooms_[room].end()) {
        Logger::warn() << user << " already joined " << room << ".";
        return;
    }

    clientDB_[user] = ws;
    for (auto u : rooms_[room]) {
        SendJoinNotification(u, user);
    }
    rooms_[room].push_back(user);
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
    const std::string& whoLeft
) {
    auto ws = clientDB_.find(whoToNotify);
    if (ws == clientDB_.end()) {
        Logger::warn() << "User: " + whoToNotify + " not found.";
        return;
    }

    Logger::info() << "Notifying \'" << whoToNotify << "\' that \'" << whoLeft << "\' left.";
    ws->second->send("{\"type\" : \"peer_left\", \"peerId\" : \"" + whoLeft + "\"}");
}



/**
 * @brief Removes a client from a room and notifies the remaining members.
 *
 * @param user Identifier of the departing client.
 * @param room Room from which the client is leaving.
 */
void
WebRTCServer::Leave(
    const std::string& user,
    const std::string& room
) {
    if (rooms_.find(room) == rooms_.end()) {
        Logger::warn() << "Room \'" << room << "\', does not exist.";
        return;
    }

    auto it = std::find(rooms_[room].begin(), rooms_[room].end(), user);
    if (it == rooms_[room].end()) {
        Logger::warn() << "User \'" << user << "\' sent leave, however not part of room \'" << room << "\'.";
        return;
    }

    rooms_[room].erase(it);
    for (auto u : rooms_[room]) {
        SendLeaveNotification(u, user);
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
    auto user = ws->getUserData()->x_client_id;
    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "leave command with no room specification.";
        return;
    }

    Leave(user, doc["room"].GetString());
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
        !doc.HasMember("to") || !doc["to"].IsString() ||
        !doc.HasMember("file") || !doc["file"].IsObject()) {
        Logger::error() << "Invalid file command format";
        return;
    }

    auto ws = CurrentWebSocket();
    if (!ws) {
        return;
    }
    std::string fromId = ws->getUserData()->x_client_id;
    std::string toId = doc["to"].GetString();
    std::string room = doc["room"].GetString();
    if (std::find(rooms_[room].begin(), rooms_[room].end(), fromId) == rooms_[room].end() ||
        std::find(rooms_[room].begin(), rooms_[room].end(), toId) == rooms_[room].end()) {
        Logger::error() << "File command participants must both be in room \'" << room << "\'.";
        return;
    }

    doc.EraseMember("to");
    doc.AddMember(
        rapidjson::Value("from", doc.GetAllocator()),
        rapidjson::Value(fromId.c_str(), doc.GetAllocator()),
        doc.GetAllocator());

    auto toWs = clientDB_.find(toId);
    if (toWs == clientDB_.end()) {
        Logger::error() << "Unable to find socket for \'" << toId << "\'.";
        return;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!toWs->second->send(buffer.GetString())) {
        Logger::error() << "Error encountered making offer to \'" << toId << "\'.";
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
                   << "\', to \'" << doc["to"].GetString() << "\': " << file.name;
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
 * @param user Identifier associated with the connection.
 */
void
WebRTCServer::OnMessageOpen(
    const std::string& user
) {
    Logger::info() << "\'" << user << "\' connected.";
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
    auto user = ws->getUserData()->x_client_id;
    Logger::info() << "\'" << user << "\' has closed connection.";
    for (const auto& [key, value] : rooms_) {
        if (std::find(rooms_[key].begin(), rooms_[key].end(), user) != rooms_[key].end()) {
            Leave(user, key);
        }
    }
}

