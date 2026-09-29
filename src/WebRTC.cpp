#include "WebRTC.hpp"

#include "WebRTCClient.hpp"
#include "WebRTCServer.hpp"

std::unique_ptr<WebRTC>
WebRTC::Create(const PropertyBag& properties) {
    if (properties.Get<bool>("server")) {
        return std::make_unique<WebRTCServer>(properties);
    }

    return std::make_unique<WebRTCClient>(properties);
}

WebRTC::WebRTC(MessagePort::Type type, const PropertyBag& properties)
    : properties_(properties),
      messagePort_(MessagePort::Create(type, properties_.Get<std::string>("name"))) {
    messagePort_->OnOpen([this](MessagePort::Ws* ws, const std::string& peer) {
        currentWebSocket_ = ws;
        OnMessageOpen(peer);
    });
    messagePort_->OnClose([this](MessagePort::Ws* ws, int code, std::string_view message) {
        currentWebSocket_ = ws;
        OnMessageClose(code, message);
    });
    messagePort_->OnMessage([this](MessagePort::Ws* ws, const std::string& message) {
        currentWebSocket_ = ws;
        MessageRouter(message);
    });
}

bool WebRTC::Start() {
    return messagePort_->Start(
        properties_.Get<std::string>("messageAddress"),
        properties_.Get<int>("messagePort")
    );
}

void WebRTC::Stop() {
    messagePort_->Stop();
}

bool WebRTC::IsRunning() {
    return messagePort_->IsRunning();
}

bool
GetPeers(
    const std::string& room,
    std::vector<std::string>& peers
) {
    return false;
}

/**
 * @brief Handles a join command and adds the client to the requested room.
 *
 * @param peer User that wants to join room
 * @param room Room the peer wants to join
 * @result true if successfully joined room.
 */
bool
WebRTC::Join(
    const std::string& peer,
    const std::string& room
) {
    if (rooms_.find(room) == rooms_.end()) {
        rooms_[room] = std::vector<std::string>();
    }

    if (std::find(rooms_[room].begin(), rooms_[room].end(), peer) != rooms_[room].end()) {
        Logger::warn() << peer << " already joined " << room << ".";
        return false;
    }

    rooms_[room].push_back(peer);

    Logger::info()
        << "User \'"
        << peer
        << "\" has joined room \""
        << room
        << "\".";


    return true;
}


/**
 * @brief Handles a leave command and removes the client to the requested room.
 *
 * @param peer User that wants to leave room
 * @param room Room the peer wants to leave
 * @result true if successfully left room.
 */
bool
WebRTC::Leave(
    const std::string& peer,
    const std::string& room
) {
    if (rooms_.find(room) == rooms_.end()) {
        Logger::warn() << "Room \'" << room << "\', does not exist.";
        return false;
    }

    auto it = std::find(rooms_[room].begin(), rooms_[room].end(), peer);
    if (it == rooms_[room].end()) {
        Logger::warn() << "User \'" << peer << "\' sent leave, however not part of room \'" << room << "\'.";
        return false;
    }

    rooms_[room].erase(it);

    Logger::info()
        << "User \'"
        << peer
        << "\" has left room \""
        << room
        << "\".";

    return true;
}

