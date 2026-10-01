#ifndef WEBRTC_SERVER_HPP
#define WEBRTC_SERVER_HPP

#include "WebRTC.hpp"

#include <map>
#include <string>

/**
 * @file WebRTCServer.hpp
 * @brief Server-side WebRTC implementation that manages rooms and file transfer coordination.
 */

/**
 * @class WebRTCServer
 * @brief Concrete WebRTC endpoint used by the signalling server.
 *
 * The server tracks connected clients by peer identifier, maintains room
 * membership, and forwards room-join, leave, ping, and file-transfer events to
 * the appropriate client. It is responsible for coordinating the start of a
 * WebRTC transfer between peers.
 */
class WebRTCServer final : public WebRTC {
public:
    /**
     * @brief Creates a server instance configured from the runtime property bag.
     *
     * @param properties Server configuration including room and signalling settings.
     */
    explicit WebRTCServer(const PropertyBag& properties)
        : WebRTC(MessagePort::Type::Server, properties) {}

    /**
     * @brief Called when a peer connects to the server's message channel.
     * @param peer Identifier of the connected peer.
     */
    void OnMessageOpen(const std::string& peer) override;

    /**
     * @brief Called when a peer disconnects or the message socket closes.
     *
     * @param code Close code from the transport layer.
     * @param message Close reason or status message.
     */
    void OnMessageClose(int code, std::string_view message) override;

protected:

    /**
     * @brief Handles a client join request and room registration.
     * @param doc Parsed JSON document containing the room and peer details.
     */
    void OnJoin(rapidjson::Document& doc) override;

    /**
     * @brief Handles a client leave request and room cleanup.
     * @param doc Parsed JSON document describing the peer and room to leave.
     */
    void OnLeave(rapidjson::Document& doc) override;

    /**
     * @brief Handles a file-accept decision sent by a peer.
     * @param doc JSON payload representing the acceptance verdict.
     */
    void OnFileAccept(rapidjson::Document& doc) override;

    /**
     * @brief Handles an incoming file-offer message and begins transfer coordination.
     * @param doc JSON payload containing file metadata and negotiation details.
     */
    void OnFileOffer(rapidjson::Document& doc) override;

    /**
     * @brief Handles ping and pong control events.
     * @param doc Parsed JSON document with ping or pong state.
     */
    void OnPingPong(rapidjson::Document& doc) override;

private:
    /**
     * @brief Metadata associated with a file transfer request.
     */
    struct FileDetails {
        std::string name;
        size_t size;
        std::string mimeType;
    };

    /**
     * @brief Sends a notification to all peers in a room that a new peer joined.
     *
     * @param whoToNotify Peer that should receive the notification.
     * @param whoJoined New peer that joined the room.
     * @param room Room in which the join occurred.
     */
    void SendJoinNotification(const std::string& whoToNotify, const std::string& whoJoined, const std::string& room);

    /**
     * @brief Sends a leave notification to a peer in a room.
     *
     * @param whoToNotify Peer to notify.
     * @param whoLeft Peer that departed the room.
     * @param room Room affected by the change.
     */
    void SendLeaveNotification(const std::string& whoToNotify, const std::string& whoLeft, const std::string& room);

    /**
     * @brief Removes a peer from a room using the server's room-tracking tables.
     *
     * @param peer Peer identifier to remove.
     * @param room Room the peer should leave.
     */
    void Leave(const std::string& peer, const std::string& room);

    /**
     * @brief Parses and stores file metadata extracted from a transport message.
     *
     * @param doc JSON payload that contains file information.
     * @param file Output structure receiving file details.
     */
    void HandleFile(rapidjson::Document& doc, FileDetails& file);

    /**
     * @brief Maps a client identity to its corresponding websocket reference.
     */
    std::map<std::string, MessagePort::Ws*> m_clientDB;
};

#endif // WEBRTC_SERVER_HPP
