#ifndef CONTROL_PORT_HPP
#define CONTROL_PORT_HPP

#include <App.h>
#include <atomic>
#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <string_view>

/**
 * @file MessagePort.hpp
 * @brief Abstract transport interface for client/server signalling sockets.
 *
 * This layer abstracts the underlying websocket implementation and exposes the
 * callback-based API used by the WebRTC stack to send and receive control
 * messages. Concrete implementations provide either a server listener or a
 * client connection for the message relay.
 */

/**
 * @class MessagePort
 * @brief Common websocket transport contract for CloudNode signalling.
 *
 * The class provides a small, implementation-agnostic interface for starting a
 * message endpoint, installing event handlers, sending text messages, and
 * checking whether the socket remains active. It also carries a peer identifier
 * used by the server-side transport to map connected clients to websocket
 * instances.
 */
class MessagePort {
public:
    /**
     * @brief Per-socket metadata attached to each websocket connection.
     */
    struct PerSocketData {
        std::string x_client_id;
    };

    /**
     * @brief Alias for the underlying uWebSockets websocket type.
     */
    using Ws = uWS::WebSocket<false, true, PerSocketData>;

    /**
     * @brief Callback invoked when a text message is received.
     */
    using MessageCallback = std::function<void(Ws* /*ws*/, const std::string&)>;

    /**
     * @brief Callback invoked when a websocket is opened.
     */
    using OpenCallback = std::function<void(Ws* /*ws*/, const std::string&)>;

    /**
     * @brief Callback invoked when a websocket closes.
     */
    using CloseCallback = std::function<void(Ws*, int, std::string_view)>;

    /**
     * @brief Role of the transport endpoint.
     */
    enum class Type {
        Server,
        Client
    };

    /**
     * @brief Virtual destructor for polymorphic transport cleanup.
     */
    virtual ~MessagePort() = default;

    MessagePort(const MessagePort&) = delete;
    MessagePort& operator=(const MessagePort&) = delete;

    /**
     * @brief Creates a server or client transport implementation.
     *
     * @param type Endpoint role.
     * @param name Identifier used by the instance when interacting with the server.
     * @return A concrete message-port implementation.
     */
    static std::unique_ptr<MessagePort> Create(Type type, const std::string& name);

    /**
     * @brief Registers a handler for incoming messages.
     * @param callback Function invoked with the socket and message payload.
     */
    virtual void OnMessage(MessageCallback callback) = 0;

    /**
     * @brief Registers a handler for connection close events.
     * @param callback Function invoked with the socket, close code, and reason.
     */
    virtual void OnClose(CloseCallback callback) = 0;

    /**
     * @brief Registers a handler for connection open events.
     * @param callback Function invoked with the socket and peer identifier.
     */
    virtual void OnOpen(OpenCallback callback) = 0;

    /**
     * @brief Starts the transport and binds it to the supplied address and port.
     *
     * @param address Host or interface address to bind to.
     * @param port Port to listen on or connect to.
     * @return true when the transport successfully starts.
     */
    virtual bool Start(const std::string& address, int port) = 0;

    /**
     * @brief Sends a text message through the active socket.
     *
     * @param message Serialized message payload.
     * @return true when the message is accepted for transmission.
     */
    virtual bool Send(const std::string& message) = 0;

    /**
     * @brief Stops the transport and releases active connections.
     */
    virtual void Stop() = 0;

    /**
     * @brief Returns whether the socket is currently running.
     * @return true if the endpoint is active.
     */
    virtual bool IsRunning() = 0;

protected:
    /**
     * @brief Constructs a named transport instance.
     * @param name Client or peer identifier assigned to the transport.
     */
    MessagePort(const std::string& name) : m_name(name) {};

    /**
     * @brief Callback invoked when a message is received.
     */
    MessageCallback m_messageCallback;

    /**
     * @brief Callback invoked when a socket closes.
     */
    CloseCallback m_closeCallback;

    /**
     * @brief Callback invoked when a socket opens.
     */
    OpenCallback m_openCallback;

    /**
     * @brief Background thread used for server-side transport activity.
     */
    std::thread m_serverThread;

    /**
     * @brief Runtime flag indicating whether the socket is active.
     */
    std::atomic<bool> m_isRunning{false};

    /**
     * @brief Synchronizes access to shared transport state.
     */
    std::mutex m_mtx;

    /**
     * @brief Human-readable identifier for the transport instance.
     */
    std::string m_name;
};

#endif // CONTROL_PORT_HPP
