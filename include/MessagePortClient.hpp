#ifndef CONTROL_PORT_CLIENT_HPP
#define CONTROL_PORT_CLIENT_HPP

#include "MessagePort.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <memory>

/**
 * @file MessagePortClient.hpp
 * @brief Client-side websocket wrapper used for signalling connections.
 */

/**
 * @class MessagePortClient
 * @brief Message transport for a connecting client endpoint.
 *
 * This implementation provides a lightweight wrapper around an `ix::WebSocket`
 * instance and exposes the generic `MessagePort` callbacks used by the WebRTC
 * layer. It is used when the application acts as a client and opens a
 * connection to a message relay server.
 */
class MessagePortClient final : public MessagePort {
public:
    /**
     * @brief Creates a client transport with the given peer identifier.
     *
     * @param name Name or identifier associated with this client.
     */
    explicit MessagePortClient(const std::string& name) : MessagePort(name) {}

    /**
     * @brief Registers the callback invoked when a message is received.
     * @param callback Handler for inbound websocket messages.
     */
    void OnMessage(MessageCallback callback) override;

    /**
     * @brief Registers the callback invoked when the websocket closes.
     * @param callback Handler for close events.
     */
    void OnClose(CloseCallback callback) override;

    /**
     * @brief Registers the callback invoked when the websocket opens.
     * @param callback Handler for open events.
     */
    void OnOpen(OpenCallback callback) override;

    /**
     * @brief Connects to the configured server endpoint.
     *
     * @param address Host or network address of the signalling server.
     * @param port Port of the signalling server.
     * @return true when the connection attempt succeeds.
     */
    bool Start(const std::string& address, int port) override;

    /**
     * @brief Sends a text message through the client websocket.
     * @param message Serialized payload to transmit.
     * @return true if the message is queued or sent successfully.
     */
    bool Send(const std::string& message) override;

    /**
     * @brief Closes the client websocket and disables future sends.
     */
    void Stop() override;

    /**
     * @brief Reports whether the client transport is active.
     * @return true when the socket is open and running.
     */
    bool IsRunning() override;

private:
    /**
     * @brief Underlying websocket implementation used by the client transport.
     */
    std::unique_ptr<ix::WebSocket> m_socket;
};

#endif // CONTROL_PORT_CLIENT_HPP
