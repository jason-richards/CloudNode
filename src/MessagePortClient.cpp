#include "MessagePortClient.hpp"
#include "Logger.hpp"

void MessagePortClient::OnMessage(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

void MessagePortClient::OnClose(CloseCallback callback) {
    closeCallback_ = std::move(callback);
}

void MessagePortClient::OnOpen(MessageCallback callback) {
    openCallback_ = std::move(callback);
}

bool MessagePortClient::Start(int port) {
    (void)port;

    std::unique_lock<std::mutex> lock(mtx_);
    if (isRunning_) {
        return false;
    }

    isRunning_ = true;
    socket_ = std::make_unique<ix::WebSocket>();
    socket_->setUrl("ws://localhost:8080");

    ix::WebSocketHttpHeaders headers;
    headers["x-client-id"] = "CowDog82";
    socket_->setExtraHeaders(headers);

    socket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        if (!msg || msg->type != ix::WebSocketMessageType::Message) {
            return;
        }

        if (messageCallback_) {
            messageCallback_(nullptr, msg->str);
        }
    });

    serverThread_ = std::thread([this]() {
        socket_->start();
    });

    cv_.notify_one();
    return true;
}

void MessagePortClient::Stop() {
    std::unique_lock<std::mutex> lock(mtx_);
    if (!isRunning_) {
        return;
    }

    isRunning_ = false;
    if (socket_) {
        socket_->stop();
    }
    cv_.notify_one();
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

bool MessagePortClient::Wait() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return !isRunning_; });
    return true;
}

bool MessagePortClient::IsRunning() {
    std::lock_guard<std::mutex> lock(mtx_);
    return isRunning_;
}
