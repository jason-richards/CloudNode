#include "WebRTC.hpp"

#include "WebRTCClient.hpp"
#include "WebRTCServer.hpp"
#include <mutex> // Required for synchronization primitives

/**
 * @file WebRTC.cpp
 * @brief Implements shared WebRTC transport setup and room membership.
 *
 * This file selects the client or server implementation from the property
 * bag, connects MessagePort events to the virtual WebRTC callbacks, and keeps
 * the local room-to-peer membership table used by both roles.
 */

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
    // Note: If the mutex member is initialized here, it should be done in the initializer list.
    // Assuming m_roomMutex is a private member of WebRTC.

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



bool
WebRTC::Start() {
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
WebRTC::GetPeers(
    const std::string& room,
    std::vector<std::string>& peers
) {
    // Use lock guard to protect access to m_rooms
    std::lock_guard<std::mutex> lock(m_roomMutex);

    auto roomIt = m_rooms.find(room);
    if (roomIt == m_rooms.end()) {
        return false;
    }

    peers.assign(roomIt->second.begin(), roomIt->second.end());
    return true;
}


void
WebRTC::UpdatePeers(
    const std::string& room,
    std::set<std::string>& peers
) {
    // Use lock guard to protect modification of m_rooms
    std::lock_guard<std::mutex> lock(m_roomMutex);
    m_rooms[room] = std::move(peers);
}


bool
WebRTC::Join(
    const std::string& peer,
    const std::string& room
) {
    // Use lock guard to protect modification of m_rooms
    std::lock_guard<std::mutex> lock(m_roomMutex);

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



bool
WebRTC::Leave(
    const std::string& peer,
    const std::string& room
) {
    // Use lock guard to protect access and modification of m_rooms
    std::lock_guard<std::mutex> lock(m_roomMutex);

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

