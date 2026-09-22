#include "ControlPort.hpp"                                                                                              
#include <iostream>                                                                                                     
                                                                                                                        
ControlPort::ControlPort() = default;                                                                                   
                                                                                                                                                                                                                                                              
ControlPort::~ControlPort() {                                                                                              
    Stop();                                                                                                             
}                                                                                                                       
                                                                                                                        
void ControlPort::OnMessage(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

void ControlPort::OnClose(CloseCallback callback) {
    closeCallback_ = std::move(callback);
}

void ControlPort::OnOpen(MessageCallback callback) {
    openCallback_ = std::move(callback);
}

bool ControlPort::Start(int port) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (isRunning_) {
        return false;
    }
    
    isRunning_ = true;

    // Launch the uWebSockets event loop in a background thread
    serverThread_ = std::thread([this, port]() {
        uWS::App()
            .ws<PerSocketData>("/*", {
                .upgrade = [](auto *res, auto *req, auto *context) {
                    PerSocketData data;
                    data.x_client_id = req->getHeader("x-client-id");

                    // Immediately upgrading without doing anything "async" before, is simple
                    res->template upgrade<PerSocketData>(
                        std::move(data),
                        req->getHeader("sec-websocket-key"),
                        req->getHeader("sec-websocket-protocol"),
                        req->getHeader("sec-websocket-extensions"),
                        context);
                },
                .open = [this](auto* ws) {
                    // Connection opened
                    PerSocketData *userData = (PerSocketData *) ws->getUserData();
                    if (clients.find(userData->x_client_id) == clients.end()) {
                        clients[userData->x_client_id] = ws;
                    }
                    if (openCallback_)
                        openCallback_(ws, userData->x_client_id);
                },
                .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
                    // Process text frames only
                    if (opCode == uWS::OpCode::TEXT && messageCallback_) {
                        messageCallback_(ws, std::string(message));
                    }
                },
                .close = [this](auto* ws, int code, std::string_view message) {
                    // Connection closed
                    if (closeCallback_)
                        closeCallback_(ws, code, message);
                }
            })
            .listen(port, [this, port](auto* listenSocket) {
                if (listenSocket) {
                    listenSocket_ = listenSocket;
                    loop_ = uWS::Loop::get();

                    // Notify that the server has started
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        isRunning_ = true;
                    }
                    cv_.notify_one();
                } else {
                    std::cerr << "ControlPort failed to listen on port " << port << std::endl;
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

bool ControlPort::Wait() {
    std::unique_lock<std::mutex> lock(mtx_);
    // Wait for the server thread to notify that it has started or failed
    cv_.wait(lock, [this] { return !isRunning_; });
    return true;
}

void ControlPort::Stop() {
    std::unique_lock<std::mutex> lock(mtx_);
    if (!isRunning_) {
        return;
    }
    
    isRunning_ = false;

    if (loop_ && listenSocket_) {
        // uWS::Loop::defer is thread-safe and executes on the uWS event thread
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

bool ControlPort::IsRunning() {
    std::lock_guard<std::mutex> lock(mtx_);
    return isRunning_;
}
