#ifndef WEBRTC_CLIENT_HPP
#define WEBRTC_CLIENT_HPP

#include "WebRTC.hpp"
#include "LocalDescription.hpp"
#include "Queue.hpp"
#include <condition_variable>
#include <map>
#include <mutex>
#include <utility>
#include <thread>

/**
 * @file WebRTCClient.hpp
 * @brief Client-side WebRTC implementation for room joining and file transfer negotiation.
 */

/**
 * @class WebRTCClient
 * @brief Concrete WebRTC endpoint used by application clients.
 *
 * The client manages connection lifecycle, room membership, and the ping/pong
 * protocol used to keep the peer link alive. It creates an outgoing data
 * channel for a configured peer and sends initial file offers containing file
 * metadata, the local SDP description, and ICE candidates.
 */
class WebRTCClient final : public WebRTC {
public:
    /**
     * @brief Creates a client endpoint using the provided runtime configuration.
     *
     * @param properties Settings such as the signalling server, STUN server,
     *                  room name, and peer identifiers.
     */
    explicit WebRTCClient(const PropertyBag& properties)
        : WebRTC(MessagePort::Type::Client, properties) {}

    /**
     * @brief Destroys the client and stops the worker thread if it is still running.
     */
    ~WebRTCClient() override;

    /**
     * @brief Starts the client worker and opens the message transport.
     * @return true if the client enters the running state.
     */
    bool Start() override;

    /**
     * @brief Stops the client transport and discontinues background work.
     */
    void Stop() override;

    /**
     * @brief Reports whether the client is active.
     * @return true when the client is running.
     */
    bool IsRunning() override;

    /**
     * @brief Called when the message port successfully opens.
     * @param peer Identifier of the server or remote peer that opened the channel.
     */
    void OnMessageOpen(const std::string& peer) override;

    /**
     * @brief Called when the message port closes.
     *
     * @param code Close code reported by the transport.
     * @param message Optional close reason.
     */
    void OnMessageClose(int code, std::string_view message) override;

    /**
     * @brief Joins the client to a signalling room.
     *
     * @param room Identifier of the room to join.
     * @return true if the join request was sent successfully.
     */
    bool JoinRoom(const std::string& room);

    /**
     * @brief Sends a lightweight ping to confirm the transport is alive.
     * @return true if the ping message was accepted for sending.
     */
    bool SendPing();

    /**
    * @brief Queues a file and sends its file-offer payload to a peer.
     *
    * The outgoing data channel must already have been created for the peer and
    * room. This method validates the file, queues its path for transfer, and
    * sends a signalling message with its metadata and the channel's gathered
    * SDP description and ICE candidates.
     *
     * @param room Room that contains the sender and recipient.
     * @param peer Identifier of the intended file recipient.
    * @param fileDetails Path to the file that will be transferred.
     */
    void
    SendFileOffer(
        const std::string&  room,
        const std::string&  peer,
        const std::string&  fileDetails
    );


    /**
     * @brief Creates and stores the outgoing data channel for a peer in a room.
     *
     * Configures the peer connection, gathers its initial SDP description and
     * ICE candidates, and installs callbacks to transfer queued files when the
     * channel opens.
     *
     * @param peer Identifier of the intended remote peer.
     * @param room Room containing the local client and remote peer.
     * @return true if the data channel and initial description were created;
     *         otherwise false.
     */
    bool
    MakeDataChannel(
        const std::string& peer,
        const std::string& room
    );

protected:

    /**
     * @brief Handles a join notification or join response.
     * @param doc JSON payload for the join event.
     */
    void OnJoin(rapidjson::Document& doc) override;

    /**
     * @brief Handles a leave notification from the server or peer.
     * @param doc JSON payload for the leave event.
     */
    void OnLeave(rapidjson::Document& doc) override;

    /**
     * @brief Handles a file-accept acknowledgement.
     * @param doc JSON payload returned by the remote side accepting a transfer.
     */
    void OnFileAccept(rapidjson::Document& doc) override;
    void OnAnswer(rapidjson::Document& doc) override;
    void OnCandidate(rapidjson::Document& doc) override;

    /**
     * @brief Handles a remote file offer received by the client.
     * @param doc JSON payload describing the incoming file transfer.
     */
    void OnFileOffer(rapidjson::Document& doc) override;

    /**
     * @brief Handles ping and pong protocol messages.
     * @param doc JSON payload carrying ping or pong state.
     */
    void OnPingPong(rapidjson::Document& doc) override;

private:
    /**
     * @brief Tracks the client lifecycle state as work progresses.
     */
    enum class ClientState {
        CLIENT_INIT,
        CLIENT_WAITING_FOR_JOIN,
        CLIENT_JOINED,
        CLIENT_RUN,
        CLIENT_PING,
        CLIENT_WAIT_FOR_PONG,
        CLIENT_WAIT_FOR_ACCEPT,
        CLIENT_EXIT
    };

    /**
     * @brief Starts the background client worker thread and process loop.
     */
    void StartClient();

    std::shared_ptr<rtc::PeerConnection> FindPeerConnection(
        const std::string& peer,
        const std::string& room
    ) const;

    /**
     * @brief Synchronizes state transitions in the client lifecycle.
     */
    std::mutex m_clientStateMutex;

    /**
     * @brief Notifies waiters when the client state changes.
     */
    std::condition_variable m_clientStateChanged;

    /**
     * @brief Background thread responsible for the client worker loop.
     */
    std::thread m_clientThread;

    /**
     * @brief Current lifecycle state of the client.
     */
    ClientState m_clientState{ClientState::CLIENT_INIT};

    /**
     * @brief Tracks whether the transport is open and ready for signalling.
     */
    bool m_transportOpen{false};

    /**
     * @brief Room currently joined by the client.
     */
    std::string m_room;

    /**
     * @brief Current peer the client is interacting with.
     */
    std::string m_peer;

    /**
     * @brief Current file target associated with the client workflow.
     */
    std::string m_file;

    using PeerRoomKey = std::pair<std::string, std::string>;
    mutable std::mutex m_dataChannelsMutex;
    std::map<PeerRoomKey, DataChannel> m_dataChannels;
};

#endif // WEBRTC_CLIENT_HPP
