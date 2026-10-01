#ifndef CONTROL_PORT_SERVER_HPP
#define CONTROL_PORT_SERVER_HPP

#include "MessagePort.hpp"

#include <map>

/**
 * @file MessagePortServer.hpp
 * @brief Server-side websocket wrapper used as the signalling hub.
 */

/**
 * @class MessagePortServer
 * @brief Message transport for a listening server endpoint.
 *
 * This implementation binds to an address and port, accepts incoming websocket
 * connections, tracks them by client identifier, and emits the callback events
 * expected by the app's signalling layer. It is used when CloudNode runs in
 * server mode and coordinates room membership and peer notifications.
 */
class MessagePortServer final : public MessagePort {
public:
    /**
     * @brief Creates a server transport with the given instance name.
     *
     * @param name Identifier used to label the server instance.
     */
    explicit MessagePortServer(const std::string& name) : MessagePort(name) {}

    /**
     * @brief Destroys the server and ensures listening sockets are released.
     */
    ~MessagePortServer() override;

    /**
     * @brief Registers the callback invoked on inbound message receipt.
     * @param callback Handler for text messages from connected clients.
     */
    void OnMessage(MessageCallback callback) override;

    /**
     * @brief Registers the callback invoked on websocket close events.
     * @param callback Handler for close notifications.
     */
    void OnClose(CloseCallback callback) override;

    /**
     * @brief Registers the callback invoked when a client connects.
     * @param callback Handler for open notifications.
     */
    void OnOpen(OpenCallback callback) override;

    /**
     * @brief Binds the server to an address and begins listening for connections.
     *
     * @param address Network address or interface to listen on.
     * @param port Port to bind and accept connections from.
     * @return true if the server starts listening successfully.
     */
    bool Start(const std::string& address, int port) override;

    /**
     * @brief Sends a message to the connected peer or all active peers as appropriate.
     *
     * @param message Serialized payload to send.
     * @return true if the message was accepted for delivery.
     */
    bool Send(const std::string& message) override;

    /**
     * @brief Stops the server, closes active sockets, and tears down the listener.
     */
    void Stop() override;

    /**
     * @brief Returns whether the server listener is currently active.
     * @return true when the server is listening and ready to accept peers.
     */
    bool IsRunning() override;

private:
    /**
     * @brief Tracks connected clients by peer identifier.
     */
    std::map<std::string, Ws*> clients;

    /**
     * @brief uWebSockets event loop used by the listening server.
     */
    uWS::Loop* m_loop{nullptr};

    /**
     * @brief Native listening socket used by the server.
     */
    us_listen_socket_t* m_listenSocket{nullptr};
};

#endif // CONTROL_PORT_SERVER_HPP
