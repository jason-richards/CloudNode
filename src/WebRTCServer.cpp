#include "WebRTCServer.hpp"

#include "Logger.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <algorithm>



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

    m_clientStateDB[peer] = ClientState::CLIENT_INIT;
}



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



bool
WebRTCServer::HandleFile(
    rapidjson::Document& doc,
    FileDetails& file
) {
    (void) file;
    if (!doc.HasMember("room") || !doc["room"].IsString() ||
        !doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("file") || !doc["file"].IsObject()) {
        Logger::error() << "Invalid file command format";
        return false;
    }

    auto ws = CurrentWebSocket();
    if (!ws) {
        Logger::error() << "Invalid websocket!";
        return false;
    }
    std::string fromId = ws->getUserData()->x_client_id;
    std::string peerId = doc["peer"].GetString();
    std::string room = doc["room"].GetString();
    if (m_rooms[room].find(fromId) == m_rooms[room].end() ||
        m_rooms[room].find(peerId) == m_rooms[room].end()
    ) {
        Logger::error() << "File command participants must both be in room \'" << room << "\'.";
        return false;
    }

    doc.EraseMember("peer");
    doc.AddMember(
        rapidjson::Value("from", doc.GetAllocator()),
        rapidjson::Value(fromId.c_str(), doc.GetAllocator()),
        doc.GetAllocator()
    );

    auto toWs = m_clientDB.find(peerId);
    if (toWs == m_clientDB.end()) {
        Logger::error() << "Unable to find socket for \'" << peerId << "\'.";
        return false;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!toWs->second->send(buffer.GetString())) {
        Logger::error() << "Error encountered making offer to \'" << peerId << "\'.";
        return false;
    }

    return true;
}



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
        Logger::error() << "Invalid websocket.";
        return;
    }

    std::string id = ws->getUserData()->x_client_id;

    Logger::info()
        << "File offer from \'"
        << id
        << "\', to \'"
        << doc["peer"].GetString()
        << "\': "
        << file.name;

    if (m_clientStateDB[id] == ClientState::CLIENT_FILE_SEND) {
        Logger::error()
            << "User \'"
            << id
            << "\" already in CLIENT_FILE_SEND state!";
        return;
    }

    if (HandleFile(doc, file)) {
        m_clientStateDB[id] = ClientState::CLIENT_FILE_SEND;
    }
}


void
WebRTCServer::OnFileComplete(
    rapidjson::Document& doc
) {
    if (!doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::error() << "Invalid file completion command format.";
        return;
    }

    auto source = CurrentWebSocket();
    if (!source) {
        Logger::error() << "Invalid websocket forwarding file completion.";
        return;
    }

    const std::string from = source->getUserData()->x_client_id;
    const std::string peer = doc["peer"].GetString();
    const std::string room = doc["room"].GetString();
    auto roomIt = m_rooms.find(room);
    if (roomIt == m_rooms.end() ||
        roomIt->second.find(from) == roomIt->second.end() ||
        roomIt->second.find(peer) == roomIt->second.end()) {
        Logger::error() << "File completion participants must both be in room '" << room << "'.";
        return;
    }

    auto recipient = m_clientDB.find(peer);
    if (recipient == m_clientDB.end()) {
        Logger::error() << "User: " + peer + " not found.";
        return;
    }

    doc.EraseMember("peer");
    doc.AddMember(
        rapidjson::Value("from", doc.GetAllocator()),
        rapidjson::Value(from.c_str(), doc.GetAllocator()),
        doc.GetAllocator()
    );

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!recipient->second->send(buffer.GetString())) {
        Logger::error() << "Error forwarding file completion to '" << peer << "'.";
        return;
    }

    m_clientStateDB[from] = ClientState::CLIENT_INIT;
}

void
WebRTCServer::OnOffer(
    rapidjson::Document& doc
) {
    Logger::info() << "OnOffer.";

    if (!doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("room") || !doc["room"].IsString() ||
        !doc.HasMember("descriptions") || !doc["descriptions"].IsArray()) {
        Logger::error() << "Invalid offer command format";
        return;
    }

    std::string peer = doc["peer"].GetString();
    std::string room = doc["room"].GetString();
    auto roomIt = m_rooms.find(room);
    auto source = CurrentWebSocket();
    if (!source || roomIt == m_rooms.end()) {
        Logger::error() << "Invalid offer source or room.";
        return;
    }

    const std::string from = source->getUserData()->x_client_id;
    if (roomIt->second.find(from) == roomIt->second.end() ||
        roomIt->second.find(peer) == roomIt->second.end()) {
        Logger::error() << "Offer participants must both be in room '" << room << "'.";
        return;
    }

    auto ws = m_clientDB.find(peer);
    if (ws == m_clientDB.end()) {
        Logger::error() << "User: " + peer + " not found.";
        return;
    }

    doc.EraseMember("peer");
    doc.AddMember(
        rapidjson::Value("from", doc.GetAllocator()),
        rapidjson::Value(from.c_str(), doc.GetAllocator()),
        doc.GetAllocator()
    );

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!ws->second->send(buffer.GetString())) {
        Logger::error() << "Error encountered forwarding offer to \'" << peer << "\'.";
        return;
    }
}


void
WebRTCServer::OnAnswer(
    rapidjson::Document& doc
) {
    Logger::info() << "OnAnswer.";

    if (!doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("sdp") || !doc["sdp"].IsString()) {
        Logger::error() << "Invalid answer command format";
        return;
    }

    std::string peer = doc["peer"].GetString();
    auto ws = m_clientDB.find(peer);
    if (ws == m_clientDB.end()) {
        Logger::error() << "User: " + peer + " not found.";
        return;
    }

    auto source = CurrentWebSocket();
    if (!source) {
        Logger::error() << "Invalid websocket forwarding answer.";
        return;
    }
    const std::string from = source->getUserData()->x_client_id;

    doc.EraseMember("peer");
    doc.AddMember(
        rapidjson::Value("from", doc.GetAllocator()),
        rapidjson::Value(from.c_str(), doc.GetAllocator()),
        doc.GetAllocator()
    );

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!ws->second->send(buffer.GetString())) {
        Logger::error() << "Error encountered forwarding answer to \'" << peer << "\'.";
        return;
    }
}

void
WebRTCServer::OnCandidate(
    rapidjson::Document& doc
) {
    Logger::info() << "OnCandidate.";

    if (!doc.HasMember("peer") || !doc["peer"].IsString() ||
        !doc.HasMember("candidate") || !doc["candidate"].IsString() ||
        !doc.HasMember("mid") || !doc["mid"].IsString()) {
        Logger::error() << "Invalid candidate command format";
        return;
    }

    std::string peer = doc["peer"].GetString();

    auto ws = m_clientDB.find(peer);
    if (ws == m_clientDB.end()) {
        Logger::error() << "User: " + peer + " not found.";
        return;
    }

    auto source = CurrentWebSocket();
    if (!source) {
        Logger::error() << "Invalid websocket forwarding candidate.";
        return;
    }
    const std::string from = source->getUserData()->x_client_id;

    doc.EraseMember("peer");
    doc.AddMember(
        rapidjson::Value("from", doc.GetAllocator()),
        rapidjson::Value(from.c_str(), doc.GetAllocator()),
        doc.GetAllocator()
    );
    
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    if (!ws->second->send(buffer.GetString())) {
        Logger::error() << "Error encountered forwarding answer to \'" << peer << "\'.";
        return;
    }
}


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
        Logger::error() << "Invalid websocket.";
        return;
    }

    std::string id = ws->getUserData()->x_client_id;

    Logger::info()
        << "File offer accept from \'"
        << id
        << "\', to \'"
        << doc["to"].GetString()
        << "\': "
        << file.name;

    if (m_clientStateDB[id] == ClientState::CLIENT_WAIT_FOR_FILE) {
        Logger::error()
            << "User \'"
            << id
            << "\" already in CLIENT_WAIT_FOR_FILE state!";
        return;
    }

    if (HandleFile(doc, file)) {
        m_clientStateDB[id] = ClientState::CLIENT_WAIT_FOR_FILE;
    }
}



void
WebRTCServer::OnMessageOpen(
    const std::string& peer
) {
    Logger::info() << "\'" << peer << "\' connected.";
}



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

