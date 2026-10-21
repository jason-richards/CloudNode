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
     * @brief Relays a file rejection and releases the sender's transfer slot.
     * @param doc JSON payload representing the rejected file.
     */
    void OnFileReject(rapidjson::Document& doc) override;

    /**
     * @brief Validates and relays an SDP offer to a peer in the same room.
     * @param doc Offer payload containing target peer, room, SDP, and candidates.
     */
    void OnOffer(rapidjson::Document& doc) override;

    /**
     * @brief Relays an SDP answer to its target peer.
     * @param doc Answer payload containing target peer and SDP.
     */
    void OnAnswer(rapidjson::Document& doc) override;

    /**
     * @brief Relays an ICE candidate to its target peer.
     * @param doc Candidate payload containing target peer, candidate, and MID.
     */
    void OnCandidate(rapidjson::Document& doc) override;

    /**
     * @brief Validates and forwards incoming file metadata to the target peer.
     * @param doc JSON payload containing target peer, room, and file metadata.
     */
    void OnFileOffer(rapidjson::Document& doc) override;

    /**
     * @brief Relays file completion and releases the sender's transfer state.
     * @param doc Completion payload containing target peer and room.
     */
    void OnFileComplete(rapidjson::Document& doc) override;

    /**
     * @brief Handles ping and pong control events.
     * @param doc Parsed JSON document with ping or pong state.
     */
    void OnPingPong(rapidjson::Document& doc) override;

private:
    enum class ClientState {
        CLIENT_INIT,
        CLIENT_FILE_SEND,
        CLIENT_WAIT_FOR_FILE,
        CLIENT_EXIT
    };
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
    void SendJoinNotification(
        const std::string& whoToNotify,
        const std::string& whoJoined,
        const std::string& room,
        const std::set<std::string>& peers
    );

    /**
     * @brief Sends a leave notification to a peer in a room.
     *
     * @param whoToNotify Peer to notify.
     * @param whoLeft Peer that departed the room.
     * @param room Room affected by the change.
     */
    void SendLeaveNotification(
        const std::string& whoToNotify,
        const std::string& whoLeft,
        const std::string& room,
        const std::set<std::string>& peers
    );

    /**
     * @brief Removes a peer from a room using the server's room-tracking tables.
     *
     * @param peer Peer identifier to remove.
     * @param room Room the peer should leave.
     */
    void Leave(const std::string& peer, const std::string& room);

    /**
     * @brief Validates and forwards a file message to its intended room peer.
     *
        * Rewrites the target peer field as the sender identity before relaying.
    *
        * @param doc JSON payload containing room, target peer, and file metadata.
        * @param file Parsed file metadata associated with the request.
        * @return true if the payload was forwarded successfully; otherwise false.
     */
    bool HandleFile(rapidjson::Document& doc, FileDetails& file);

    /**
     * @brief Maps a client identity to its corresponding websocket reference.
     */
    std::map<std::string, MessagePort::Ws*> m_clientDB;
    std::map<std::string, ClientState> m_clientStateDB;

};

#endif // WEBRTC_SERVER_HPP
