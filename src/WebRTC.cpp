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
        : m_properties(properties),
            m_messagePort(MessagePort::Create(type, m_properties.Get<std::string>("name"))) {
        m_messagePort->OnOpen([this](MessagePort::Ws* ws, const std::string& peer) {
                m_currentWebSocket = ws;
        OnMessageOpen(peer);
    });
    m_messagePort->OnClose([this](MessagePort::Ws* ws, int code, std::string_view message) {
        m_currentWebSocket = ws;
        OnMessageClose(code, message);
    });
    m_messagePort->OnMessage([this](MessagePort::Ws* ws, const std::string& message) {
        m_currentWebSocket = ws;
        MessageRouter(message);
    });
}

bool WebRTC::Start() {
    return m_messagePort->Start(
        m_properties.Get<std::string>("messageAddress"),
        m_properties.Get<int>("messagePort")
    );
}

void WebRTC::Stop() {
    m_messagePort->Stop();
}

bool WebRTC::IsRunning() {
    return m_messagePort->IsRunning();
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
    auto& peers = m_rooms[room];
    if (!peers.insert(peer).second) {
        Logger::warn() << peer << " already joined " << room << ".";
        return false;
    }

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
    if (m_rooms.find(room) == m_rooms.end()) {
        Logger::warn() << "Room \'" << room << "\', does not exist.";
        return false;
    }

    auto& peers = m_rooms[room];
    auto it = peers.find(peer);
    if (it == peers.end()) {
        Logger::warn() << "User \'" << peer << "\' sent leave, however not part of room \'" << room << "\'.";
        return false;
    }

    peers.erase(it);

    Logger::info()
        << "User \'"
        << peer
        << "\" has left room \""
        << room
        << "\".";

    return true;
}

