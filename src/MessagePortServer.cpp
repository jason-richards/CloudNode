#include "MessagePortServer.hpp"

#include <iostream>

MessagePortServer::~MessagePortServer() {
    Stop();
}

void MessagePortServer::OnMessage(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

void MessagePortServer::OnClose(CloseCallback callback) {
    closeCallback_ = std::move(callback);
}

void MessagePortServer::OnOpen(OpenCallback callback) {
    openCallback_ = std::move(callback);
}

bool MessagePortServer::Start(const std::string& address, int port) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (isRunning_) {
        return false;
    }

    serverThread_ = std::thread([this, port]() {
        uWS::App()
            .ws<PerSocketData>("/*", {
                .upgrade = [](auto *res, auto *req, auto *context) {
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
                    if (clients.find(userData->x_client_id) == clients.end()) {
                        clients[userData->x_client_id] = ws;
                    }
                    if (openCallback_) {
                        openCallback_(ws, userData->x_client_id);
                    }
                },
                .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
                    if (opCode == uWS::OpCode::TEXT && messageCallback_) {
                        messageCallback_(ws, std::string(message));
                    }
                },
                .close = [this](auto* ws, int code, std::string_view message) {
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
                    std::cerr << "MessagePort failed to listen on port " << port << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        isRunning_ = false;
                    }
                }
            })
            .run();
    });

    return true;
}

bool MessagePortServer::Send(const std::string& message) {
    (void) message;
    return false;
}

void MessagePortServer::Stop() {
    uWS::Loop* loop = nullptr;
    us_listen_socket_t* listenSocket = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!isRunning_ && !serverThread_.joinable()) {
            return;
        }

        isRunning_ = false;
        loop = loop_;
        listenSocket = listenSocket_;
    }

    if (loop && listenSocket) {
        loop->defer([this, listenSocket]() {
            us_listen_socket_close(0, listenSocket);
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (listenSocket_ == listenSocket) {
                    listenSocket_ = nullptr;
                }
            }
        });
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

bool MessagePortServer::IsRunning() {
    std::lock_guard<std::mutex> lock(mtx_);
    return isRunning_;
}
