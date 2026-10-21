#ifndef WEBRTC_CLIENT_HPP
#define WEBRTC_CLIENT_HPP

#include "WebRTC.hpp"
#include "LocalDescription.hpp"
#include "Queue.hpp"
#include <condition_variable>
#include <cstdint>
#include <fstream>
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
 * The client manages connection lifecycle, room membership, peer negotiation,
 * file transfers, and the ping/pong protocol used to keep the signalling
 * transport alive.
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
     * @brief Queues a file and sends its metadata offer to a peer.
     *
        * The outgoing data channel must already have been created for the peer and
        * room. This method validates the file, sends a `file_offer` signalling
        * message containing its metadata, and queues the path for transmission only
        * after the peer accepts. SDP and ICE candidates are sent separately during
        * channel creation.
     *
     * @param room Room that contains the sender and recipient.
     * @param peer Identifier of the intended file recipient.
     * @param fileDetails Path to the file that will be transferred.
     * @return true if the offer was sent and the file queued.
     */
    bool
    SendFileOffer(
        const std::string&  room,
        const std::string&  peer,
        const std::string&  fileDetails
    );


    /** @brief Appends a received binary chunk to the file offered by peer in room. */
    void
    OnIncomingChunk(
        const std::string& peer,
        const std::string& room,
        const rtc::binary& bytes
    );

    void
    SendFileAccept(
        rapidjson::Document& doc
    );

    void
    SendFileReject(
        rapidjson::Document& doc
    );

    /**
     * @brief Signal file transfer complete.
     *
     * This method is called when the file has been completely chunked and send over
     * the data channel.
     *
     * @param room Room that contains the sender and recipient.
     * @param peer Identifier of the intended file recipient.
     * @param fileDetails Path to the file that will be transferred.
     */
    void
    SendFileComplete(
        const std::string& room,
        const std::string& peer
    );


    /**
     * @brief Creates an outgoing data channel and initiates peer negotiation.
     *
     * Configures the peer connection, gathers its initial SDP description and
     * ICE candidates, stores the channel, and sends them to the peer in an
     * `offer` message. When the channel opens, a worker sends each file path
        * added to the channel's queue after its offer is accepted and signals
        * completion after each transfer.
     *
     * @param peer Identifier of the intended remote peer.
     * @param room Room containing the local client and remote peer.
    * @return true if the channel was created and the offer was sent;
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

    /** @brief Signals that the peer accepted the pending file offer. */
    void OnFileAccept(rapidjson::Document& doc) override;

    /**
     * @brief Releases the pending file transfer after a rejection.
     * @param doc JSON payload returned by the rejecting peer.
     */
    void OnFileReject(rapidjson::Document& doc) override;

    /**
     * @brief Applies an incoming SDP offer and sends the corresponding answer.
     * @param doc Offer payload containing sender, room, descriptions, and candidates.
     */
    void OnOffer(rapidjson::Document& doc) override;

    /**
     * @brief Applies a remote SDP answer to the matching peer connection.
     * @param doc Answer payload containing sender, room, and SDP.
     */
    void OnAnswer(rapidjson::Document& doc) override;

    /**
     * @brief Applies a remote ICE candidate to the matching peer connection.
     * @param doc Candidate payload containing sender, room, candidate, and MID.
     */
    void OnCandidate(rapidjson::Document& doc) override;

    /**
     * @brief Handles file metadata sent for an incoming transfer.
     * @param doc JSON payload containing sender, room, and file metadata.
     */
    void OnFileOffer(rapidjson::Document& doc) override;

    /**
     * @brief Handles notice that a remote peer completed a file transfer.
     * @param doc JSON payload containing sender and room.
     */
    void OnFileComplete(rapidjson::Document& doc) override;

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
        CLIENT_WAIT_FOR_FILE_ACCEPT,
        CLIENT_EXIT
    };

    /**
     * @brief Starts the background client worker thread and process loop.
         *
         * Loads runtime properties, waits for the transport, performs room and
         * peer initialization, and runs the keepalive state machine.
     */
    void StartClient();

    using PeerRoomKey = std::pair<std::string, std::string>;

    /** @brief An offered file awaiting accept or reject. */
    struct PendingOffer {
        PeerRoomKey key;
        std::string path;
    };

    /** @brief Removes the pending offer named by the response's uuid; false if none. */
    bool TakePendingOffer(const rapidjson::Document& doc, PendingOffer& offer);

    /** @brief Wakes transfer workers waiting for an accept/reject decision. */
    void WakeFileTransferWorkers();

    /**
     * @brief Creates a peer connection using the configured ICE and message settings.
     *
     * The peer and room identify the connection in logs; connection settings,
     * including the maximum message size and STUN server, are read from the
     * client's property bag.
     *
     * @param peer Remote peer identifier associated with this connection.
     * @param room Room associated with this connection.
     * @return A configured peer connection.
     */
    std::shared_ptr<rtc::PeerConnection> MakePeerConnection(
        const std::string& peer,
        const std::string& room
    );

    /**
     * @brief Finds the peer connection associated with a peer and room.
     * @param peer Remote peer identifier.
     * @param room Room identifier.
     * @return The matching connection, or nullptr if none is stored.
     */
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

    std::mutex m_pendingMutex;
    std::map<std::string, PendingOffer> m_pendingOffers;

    mutable std::mutex m_dataChannelsMutex;
    std::map<PeerRoomKey, DataChannel> m_dataChannels;

    /** @brief Guards m_incomingFiles and writes to the open files. */
    std::mutex m_incomingMutex;

    struct IncomingFile {
        std::ofstream stream;
        std::int64_t expectedSize{0};
    };

    /** @brief Files currently being received, keyed by sender and room. */
    std::map<PeerRoomKey, IncomingFile> m_incomingFiles;
};

#endif // WEBRTC_CLIENT_HPP
