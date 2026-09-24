#include "WebRTCClient.hpp"
#include "Logger.hpp"

void
WebRTCClient::Join(
    MessagePort::Ws * ws,
    rapidjson::Document& doc
) {
    Logger::info() << "Join";
}

void
WebRTCClient::Leave(
    MessagePort::Ws * ws,
    rapidjson::Document& doc
) {
    Logger::info() << "Leave";
}

void
WebRTCClient::FileAccept(
    MessagePort::Ws * ws,
    rapidjson::Document& doc
) {
    Logger::info() << "FileAccept";
}

void
WebRTCClient::FileOffer(
    MessagePort::Ws * ws,
    rapidjson::Document& doc
) {
    Logger::info() << "FileOffer";
}

void
WebRTCClient::Ping(
    MessagePort::Ws * ws,
    rapidjson::Document& doc
) {
    Logger::info() << "Ping";
}

void
WebRTCClient::MessageOpen(
    MessagePort::Ws * ws,
    const std::string& user
) {
    Logger::info() << "MessageOpen";
}



/**
 * @brief Handles a closed WebSocket connection and removes its room membership.
 *
 * @param ws WebSocket that was closed.
 * @param code WebSocket close code.
 * @param message WebSocket close message.
 */
void
WebRTCClient::MessageClose(
    MessagePort::Ws * ws,
    int code,
    std::string_view message
) {
}
