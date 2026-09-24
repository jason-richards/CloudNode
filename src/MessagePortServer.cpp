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

void MessagePortServer::OnOpen(MessageCallback callback) {
    openCallback_ = std::move(callback);
}

bool MessagePortServer::Start(int port) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (isRunning_) {
        return false;
    }

    isRunning_ = true;
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
                    cv_.notify_one();
                } else {
                    std::cerr << "MessagePort failed to listen on port " << port << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        isRunning_ = false;
                    }
                    cv_.notify_one();
                }
            })
            .run();
    });

    return isRunning_;
}

bool MessagePortServer::Wait() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return !isRunning_; });
    return true;
}

void MessagePortServer::Stop() {
    std::unique_lock<std::mutex> lock(mtx_);
    if (!isRunning_) {
        return;
    }

    isRunning_ = false;
    if (loop_ && listenSocket_) {
        loop_->defer([this]() {
            if (listenSocket_) {
                us_listen_socket_close(0, listenSocket_);
                listenSocket_ = nullptr;
            }
        });
    }

    cv_.notify_one();
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

bool MessagePortServer::IsRunning() {
    std::lock_guard<std::mutex> lock(mtx_);
    return isRunning_;
}
