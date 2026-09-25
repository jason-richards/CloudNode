#include "WebRTCClient.hpp"
#include "Logger.hpp"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"


void
WebRTCClient::JoinRoom(
    const std::string& room
) {
    WaitUntilRunning();

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


void
WebRTCClient::SendPing() {
    WaitUntilRunning();
    if (!SendMessage("{ \"type\" : \"ping\" }")) {
        Logger::error() << "Unable to send ping.";
    }
}

void
WebRTCClient::OnJoin(
    rapidjson::Document& doc
) {
    (void) doc;
    Logger::info() << "Join";
}

void
WebRTCClient::OnLeave(
    rapidjson::Document& doc
) {
    (void) doc;
    Logger::info() << "Leave";
}

void
WebRTCClient::OnFileAccept(
    rapidjson::Document& doc
) {
    (void) doc;
    Logger::info() << "FileAccept";
}

void
WebRTCClient::OnFileOffer(
    rapidjson::Document& doc
) {
    (void) doc;
    Logger::info() << "FileOffer";
}

void
WebRTCClient::OnPingPong(
    rapidjson::Document& doc
) {
    (void) doc;
    Logger::info() << "PingPong";
}


void
WebRTCClient::OnMessageOpen(
    const std::string& user
) {
    (void) user;
    Logger::info() << "MessagePort Opened.";
}



/**
 * @brief Handles a closed WebSocket connection and removes its room membership.
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
