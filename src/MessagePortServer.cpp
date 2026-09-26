#include "MessagePortServer.hpp"

#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <map>

/**
 * @brief Destructor for MessagePortServer.
 * 
 * Ensures that the WebSocket server is properly stopped and resources are cleaned up
 * when the object goes out of scope. Calls Stop() internally.
 */
MessagePortServer::~MessagePortServer() {
    Stop();
}

/**
 * @brief Sets the callback function to be executed when a message is received on the WebSocket connection.
 * 
 * @param callback The callback lambda/function object that handles incoming text messages.
 *        It receives the WebSocket pointer and the message string.
 */
void MessagePortServer::OnMessage(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

/**
 * @brief Sets the callback function to be executed when a client disconnects.
 * 
 * @param callback The callback lambda/function object that handles connection closure.
 *        It receives the WebSocket pointer, the close code, and the message view.
 */
void MessagePortServer::OnClose(CloseCallback callback) {
    closeCallback_ = std::move(callback);
}

/**
 * @brief Sets the callback function to be executed when a new client successfully connects.
 * 
 * @param callback The callback lambda/function object that handles connection opening.
 *        It receives the WebSocket pointer and the unique client ID associated with the connection.
 */
void MessagePortServer::OnOpen(OpenCallback callback) {
    openCallback_ = std::move(callback);
}

/**
 * @brief Starts the WebSocket server on a specified address and port.
 * 
 * This method initializes and runs the uWS application in a separate thread.
 * It sets up handlers for connection open, incoming messages, and disconnections.
 * The server listens for connections at the given port.
 * 
 * @param address The network interface address to bind to (e.g., "0.0.0.0").
 * @param port The port number to listen on.
 * @return true if the server started successfully and is listening, false otherwise.
 */
bool MessagePortServer::Start(const std::string& address, int port) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (isRunning_) {
        // Server is already running
        return false;
    }

    serverThread_ = std::thread([this, port]() {
        uWS::App()
            .ws<PerSocketData>("/*", {
                .upgrade = [](auto *res, auto *req, auto *context) {
                    // Extract client ID from header and upgrade the connection data structure
                    PerSocketData data;
                    data.x_client_id = req->getHeader("x-client-id");
                    res->template upgrade<PerSocketData>(
                        std::move(data),
                        req->getHeader("sec-websocket-key"),
                        req->getHeader("sec-websocket-protocol"),
                        req->getHeader("sec-websocket-extensions"),
                        context);
                },
                .open = [this](auto* ws) {
                    PerSocketData *userData = (PerSocketData *) ws->getUserData();
                    // Track the client connection
                    if (clients.find(userData->x_client_id) == clients.end()) {
                        clients[userData->x_client_id] = ws;
                    }
                    // Execute open callback if set
                    if (openCallback_) {
                        openCallback_(ws, userData->x_client_id);
                    }
                },
                .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
                    // Handle incoming text messages and execute the callback
                    if (opCode == uWS::OpCode::TEXT && messageCallback_) {
                        messageCallback_(ws, std::string(message));
                    }
                },
                .close = [this](auto* ws, int code, std::string_view message) {
                    // Execute close callback if set
                    if (closeCallback_) {
                        closeCallback_(ws, code, message);
                    }
                }
            })
            .listen(port, [this, port](auto* listenSocket) {
                if (listenSocket) {
                    listenSocket_ = listenSocket;
                    loop_ = uWS::Loop::get();
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        isRunning_ = true;
                    }
                } else {
                    // Error handling if listening fails
                    std::cerr << "MessagePort failed to listen on port " << port << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        isRunning_ = false;
                    }
                }
            })
            .run(); // Start the uWS event loop
    });

    return true;
}

/**
 * @brief Sends a message through the established WebSocket connection (currently unimplemented/placeholder).
 * 
 * Note: The current implementation ignores the message and returns false, indicating that
 * actual sending logic needs to be implemented using the active WebSocket pointers.
 * 
 * @param message The string content to send.
 * @return bool Always returns false as a placeholder for future implementation.
 */
bool MessagePortServer::Send(const std::string& message) {
    // Placeholder: Actual sending logic should iterate over 'clients' map and use ws->send()
    (void) message; 
    return false;
}

/**
 * @brief Stops the WebSocket server gracefully.
 * 
 * This method signals the running thread to stop, closes the listening socket,
 * and waits for the background thread to join, ensuring all resources are released.
 */
void MessagePortServer::Stop() {
    uWS::Loop* loop = nullptr;
    us_listen_socket_t* listenSocket = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!isRunning_ && !serverThread_.joinable()) {
            return; // Already stopped or never started
        }

        isRunning_ = false;
        loop = loop_;
        listenSocket = listenSocket_;
    }

    // Defer the socket closing operation to the uWS loop thread
    if (loop && listenSocket) {
        loop->defer([this, listenSocket]() {
            us_listen_socket_close(0, listenSocket);
            {
                std::lock_guard<std::mutex> lock(mtx_);
                // Only clear the socket if it was the one we were tracking
                if (listenSocket_ == listenSocket) {
                    listenSocket_ = nullptr;
                }
            }
        });
    }

    // Wait for the server thread to finish its execution loop
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

/**
 * @brief Checks if the WebSocket server is currently running.
 * 
 * @return true if the internal state indicates the server is active, false otherwise.
 */
bool MessagePortServer::IsRunning() {
    std::lock_guard<std::mutex> lock(mtx_);
    return isRunning_;
}
