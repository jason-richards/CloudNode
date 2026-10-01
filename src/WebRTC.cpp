#include "WebRTC.hpp"

#include "WebRTCClient.hpp"
#include "WebRTCServer.hpp"

/**
 * @file WebRTC.cpp
 * @brief Implements shared WebRTC transport setup and room membership.
 *
 * This file selects the client or server implementation from the property
 * bag, connects MessagePort events to the virtual WebRTC callbacks, and keeps
 * the local room-to-peer membership table used by both roles.
 */

/**
 * @brief Creates the configured WebRTC role.
 *
 * The `server` property selects WebRTCServer when true; otherwise a
 * WebRTCClient is created. The concrete object is returned through the shared
 * WebRTC interface.
 *
 * @param properties Configuration values used by the selected implementation.
 * @return A newly created server or client instance.
 */
std::unique_ptr<WebRTC>
WebRTC::Create(const PropertyBag& properties) {
    if (properties.Get<bool>("server")) {
        return std::make_unique<WebRTCServer>(properties);
    }

    return std::make_unique<WebRTCClient>(properties);
}

/**
 * @brief Initializes the message transport and binds its event callbacks.
 *
 * Creates a MessagePort for the requested role using the configured local
 * name. On each transport event, the socket is recorded as the current socket
 * before the corresponding virtual callback is invoked. Incoming messages
 * are passed to MessageRouter for parsing and command dispatch.
 *
 * @param type Whether the transport operates in server or client mode.
 * @param properties Configuration values retained by this WebRTC instance.
 */
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

/**
 * @brief Starts the message transport.
 *
 * Reads the configured message address and port and delegates startup to the
 * MessagePort implementation.
 *
 * @return true if the MessagePort reports that startup was initiated
 *         successfully; otherwise false.
 */
bool WebRTC::Start() {
    return m_messagePort->Start(
        m_properties.Get<std::string>("messageAddress"),
        m_properties.Get<int>("messagePort")
    );
}

/**
 * @brief Stops the message transport.
 *
 * Delegates shutdown to MessagePort. Role-specific workers, when present, are
 * managed by the derived class.
 */
void WebRTC::Stop() {
    m_messagePort->Stop();
}

/**
 * @brief Reports whether the message transport is running.
 *
 * @return The running status reported by MessagePort.
 */
bool WebRTC::IsRunning() {
    return m_messagePort->IsRunning();
}

/**
 * @brief Placeholder for retrieving the peers in a room.
 *
 * This free-function stub is not connected to a WebRTC instance and currently
 * neither reads room membership nor modifies the output vector.
 *
 * @param room Room whose peers would be retrieved.
 * @param peers Output vector intended to receive peer identifiers.
 * @return false; peer retrieval is not implemented here.
 */
bool
GetPeers(
    const std::string& room,
    std::vector<std::string>& peers
) {
    return false;
}

/**
 * @brief Adds a peer to a room's local membership set.
 *
 * Indexing the room map creates an empty membership set for a room that does
 * not yet exist. Set insertion rejects duplicate peer identifiers.
 *
 * @param peer Identifier of the peer joining.
 * @param room Identifier of the room to join.
 * @return true if the peer was newly added; false if already a member.
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
 * @brief Removes a peer from a room's local membership set.
 *
 * The room must already exist and contain the peer. The room entry is retained
 * if its membership set becomes empty.
 *
 * @param peer Identifier of the peer leaving.
 * @param room Identifier of the room to leave.
 * @return true if the peer was removed; false if the room or peer was absent.
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

