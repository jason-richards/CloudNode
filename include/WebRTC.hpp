#ifndef WEBRTC_HPP
#define WEBRTC_HPP

#include "Logger.hpp"
#include "MessagePort.hpp"
#include "PropertyBag.hpp"

#include <rapidjson/document.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file WebRTC.hpp
 * @brief Base WebRTC session layer shared by client and server implementations.
 *
 * The class owns the shared message port, exposes configuration from a
 * `PropertyBag`, and interprets JSON message traffic originating from the
 * signalling channel. Concrete subclasses implement the behaviour specific to
 * either a client or a server peer.
 */

/**
 * @class WebRTC
 * @brief Common networking and message-routing abstraction for the CloudNode app.
 *
 * Each WebRTC instance associates a `MessagePort` with a property bag that holds
 * runtime settings such as the message server address, STUN server, room name,
 * and peer identity. The base class handles room membership tracking and routes
 * incoming JSON payloads to the appropriate virtual handler.
 */
class WebRTC {
private:
    /**
     * @brief Computes a stable hash for WebRTC message type dispatch.
     *
     * Message types are mapped to a small integer hash and compared in the router
     * so the code can switch on string labels without expensive string compares.
     */
    static constexpr unsigned int string_hash(std::string_view str) {
        unsigned int hash = 2166136261u;
        for (char c : str) {
            hash ^= static_cast<unsigned int>(c);
            hash *= 16777619u;
        }
        return hash;
    }

public:

    /**
     * @brief Destroys the WebRTC session and releases resources.
     */
    virtual ~WebRTC() = default;

    /**
     * @brief Creates the appropriate WebRTC implementation for the configured mode.
     *
     * @param properties Runtime configuration used to initialize the instance.
     * @return A concrete WebRTC instance for either the server or client role.
     */
    static std::unique_ptr<WebRTC> Create(const PropertyBag& properties);

    /**
     * @brief Starts the underlying transport and background worker.
     * @return true if startup succeeds, false otherwise.
     */
    virtual bool Start();

    /**
     * @brief Stops the current connection and shuts down any worker threads.
     */
    virtual void Stop();

    /**
     * @brief Returns whether the session is currently active.
     * @return true when the WebRTC layer is running.
     */
    virtual bool IsRunning();


    /**
     * @brief Retrieves peer identifiers currently present in a room.
     *
     * @param room Room name being queried.
     * @param peers Output vector that receives the peer names in that room.
     * @return true when the room exists and the list could be populated; false otherwise.
     */
    bool
    GetPeers(
        const std::string& room,
        std::vector<std::string>& peers
    );


    /**
     * @brief Routes an incoming JSON message to the correct handler.
     *
     * The message is parsed by RapidJSON. If the document includes a valid
     * `type` field, the router dispatches to one of the derived-class handlers
     * such as join, leave, ping, or file negotiation. Unknown message types are
     * logged and ignored.
     *
     * @param jsonStr Raw JSON payload from the signalling service.
     */
    void
    MessageRouter(
        const std::string& jsonStr
    ) {
        rapidjson::Document doc;
        doc.Parse(jsonStr.data(), jsonStr.size());

        if (doc.HasParseError() || !doc.HasMember("type") || !doc["type"].IsString()) {
            return;
        }

        std::string type = doc["type"].GetString();
        switch (string_hash(type)) {
            case string_hash("join"):
            case string_hash("peer_joined"):
                OnJoin(doc);
                break;
            case string_hash("peer_left"):
            case string_hash("leave"):
                OnLeave(doc);
                break;
            case string_hash("file_accept"):
                OnFileAccept(doc);
                break;
            case string_hash("file_offer"):
                OnFileOffer(doc);
                break;
            case string_hash("ping"):
            case string_hash("pong"):
                OnPingPong(doc);
                break;
            default:
                Logger::warn()
                    << "Unhandled message: "
                    << jsonStr;
                break;
        }
    }

    /**
     * @brief Called when a remote peer opens a message connection.
     *
     * @param peer Identifier of the peer that connected.
     */
    virtual void OnMessageOpen(const std::string& peer) = 0;

    /**
     * @brief Called when the message connection to a peer closes.
     *
     * @param code Transport close code.
     * @param message Additional close reason.
     */
    virtual void OnMessageClose(int code, std::string_view message) = 0;

protected:

    /**
     * @brief Constructs the base WebRTC object for a specific transport role.
     *
     * @param type Message port type, either client or server.
     * @param properties Configuration values used by the session.
     */
    WebRTC(MessagePort::Type type, const PropertyBag& properties);

    /**
     * @brief Handles a peer join notification or join request.
     * @param doc Parsed JSON document for the join event.
     */
    virtual void OnJoin(rapidjson::Document& doc) = 0;

    /**
     * @brief Handles a peer leave request or leave notification.
     * @param doc Parsed JSON document for the leave event.
     */
    virtual void OnLeave(rapidjson::Document& doc) = 0;

    /**
     * @brief Handles a file-accept response.
     * @param doc Parsed JSON document for file acceptance.
     */
    virtual void OnFileAccept(rapidjson::Document& doc) = 0;

    /**
     * @brief Handles a file-offer message, usually for establishing a peer-to-peer file transfer.
     * @param doc Parsed JSON file-offer payload.
     */
    virtual void OnFileOffer(rapidjson::Document& doc) = 0;

    /**
     * @brief Handles ping and pong control traffic.
     * @param doc Parsed JSON document containing ping or pong state.
     */
    virtual void OnPingPong(rapidjson::Document& doc) = 0;

    /**
     * @brief Registers a peer as part of a room membership.
     *
     * @param peer Remote peer identifier.
     * @param room Room name to join.
     * @return true if the peer was added successfully.
     */
    bool
    Join(
        const std::string& peer,
        const std::string& room
    );

    /**
     * @brief Removes a peer from a room.
     *
     * @param peer Remote peer identifier.
     * @param room Room name to leave.
     * @return true if the peer was removed successfully.
     */
    bool
    Leave(
        const std::string& peer,
        const std::string& room
    );

    /**
     * @brief Sends a raw JSON control message over the current message port.
     * @param message Serialized message payload.
     * @return true when the send succeeds.
     */
    bool SendMessage(const std::string& message) { return m_messagePort->Send(message); }

    /**
     * @brief Returns the currently active websocket for the message transport.
     * @return Active websocket pointer or nullptr if there is none.
     */
    MessagePort::Ws* CurrentWebSocket() const { return m_currentWebSocket; }

    /**
     * @brief Returns the configuration bag used to initialize this session.
     * @return Const reference to the runtime property bag.
     */
    const PropertyBag& Properties() const { return m_properties; }

    /**
     * @brief Maps rooms to the peers currently associated with each room.
     */
    std::map<std::string, std::set<std::string>> m_rooms;
private:
    /**
     * @brief Runtime configuration used by the WebRTC session.
     */
    PropertyBag m_properties;

    /**
     * @brief Transport abstraction used to send and receive signalling messages.
     */
    std::unique_ptr<MessagePort> m_messagePort;

    /**
     * @brief Pointer to the active websocket used by the message port.
     */
    MessagePort::Ws* m_currentWebSocket{nullptr};
};

#endif // WEBRTC_HPP
