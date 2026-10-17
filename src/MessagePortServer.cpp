#include "MessagePortServer.hpp"

#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <map>

MessagePortServer::~MessagePortServer() {
    Stop();
}

void MessagePortServer::OnMessage(MessageCallback callback) {
    m_messageCallback = std::move(callback);
}

void MessagePortServer::OnClose(CloseCallback callback) {
    m_closeCallback = std::move(callback);
}

void MessagePortServer::OnOpen(OpenCallback callback) {
    m_openCallback = std::move(callback);
}

bool MessagePortServer::Start(const std::string& address, int port) {
    std::unique_lock<std::mutex> lock(m_mtx);
    if (m_isRunning) {
        // Server is already running
        return false;
    }
    m_stopRequested = false;

    m_serverThread = std::thread([this, port]() {
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
                    PerSocketData *peerData = (PerSocketData *) ws->getUserData();
                    // Track the client connection
                    if (clients.find(peerData->x_client_id) == clients.end()) {
                        clients[peerData->x_client_id] = ws;
                    }
                    // Execute open callback if set
                    if (m_openCallback) {
                        m_openCallback(ws, peerData->x_client_id);
                    }
                },
                .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
                    // Handle incoming text messages and execute the callback
                    if (opCode == uWS::OpCode::TEXT && m_messageCallback) {
                        m_messageCallback(ws, std::string(message));
                    }
                },
                .close = [this](auto* ws, int code, std::string_view message) {
                    // Execute close callback if set
                    if (m_closeCallback) {
                        m_closeCallback(ws, code, message);
                    }
                }
            })
            .listen(port, [this, port](auto* listenSocket) {
                if (listenSocket) {
                    m_listenSocket = listenSocket;
                    m_loop = uWS::Loop::get();
                    {
                        std::lock_guard<std::mutex> lock(m_mtx);
                        m_isRunning = true;
                    }
                    m_runningChanged.notify_all();
                } else {
                    // Error handling if listening fails
                    std::cerr << "MessagePort failed to listen on port " << port << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(m_mtx);
                        m_isRunning = false;
                        m_stopRequested = true;
                    }
                    m_runningChanged.notify_all();
                }
            })
            .run(); // Start the uWS event loop
    });

    return true;
}

bool MessagePortServer::Send(const std::string& message) {
    // Placeholder: Actual sending logic should iterate over 'clients' map and use ws->send()
    (void) message; 
    return false;
}

void MessagePortServer::Stop() {
    uWS::Loop* loop = nullptr;
    us_listen_socket_t* listenSocket = nullptr;
    bool alreadyStopped = false;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        alreadyStopped = !m_isRunning && !m_serverThread.joinable();
        m_isRunning = false;
        m_stopRequested = true;
        loop = m_loop;
        listenSocket = m_listenSocket;
    }
    m_runningChanged.notify_all();
    if (alreadyStopped) {
        return;
    }

    // Defer the socket closing operation to the uWS loop thread
    if (loop && listenSocket) {
        loop->defer([this, listenSocket]() {
            us_listen_socket_close(0, listenSocket);
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                // Only clear the socket if it was the one we were tracking
                if (m_listenSocket == listenSocket) {
                    m_listenSocket = nullptr;
                }
            }
        });
    }

    // Wait for the server thread to finish its execution loop
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

bool MessagePortServer::IsRunning() {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_isRunning;
}
