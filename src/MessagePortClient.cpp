#include "MessagePortClient.hpp"
#include "Logger.hpp"

void MessagePortClient::OnMessage(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

void MessagePortClient::OnClose(CloseCallback callback) {
    closeCallback_ = std::move(callback);
}

void MessagePortClient::OnOpen(OpenCallback callback) {
    openCallback_ = std::move(callback);
}

bool MessagePortClient::Start(const std::string& address, int port) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (isRunning_) {
        return false;
    }

    socket_ = std::make_unique<ix::WebSocket>();
    socket_->setUrl(address + ":" + std::to_string(port));

    ix::WebSocketHttpHeaders headers;
    headers["x-client-id"] = name_;
    socket_->setExtraHeaders(headers);

    socket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        if (!msg) {
            return;
        }

        switch (msg->type) {
        case ix::WebSocketMessageType::Open:
            Logger::info() << "ix::WebSocketMessageType::Open";
            {
                std::lock_guard<std::mutex> lock(mtx_);
                isRunning_ = true;
            }
            if (openCallback_) {
                openCallback_(nullptr, name_);
            }
            break;
        case ix::WebSocketMessageType::Message:
            if (messageCallback_) {
                messageCallback_(nullptr, msg->str);
            }
            break;
        case ix::WebSocketMessageType::Close:
            Logger::info() << "ix::WebSocketMessageType::Close";
            {
                std::lock_guard<std::mutex> lock(mtx_);
                isRunning_ = false;
            }
            if (closeCallback_) {
                closeCallback_(nullptr, msg->closeInfo.code, msg->closeInfo.reason);
            }
            break;
        case ix::WebSocketMessageType::Error:
            Logger::info() << "ix::WebSocketMessageType::Error";
            {
                std::lock_guard<std::mutex> lock(mtx_);
                isRunning_ = false;
            }
            break;
        };
    });

    serverThread_ = std::thread([this]() {
        socket_->start();
    });

    return true;
}

bool MessagePortClient::Send(const std::string& message) {
    if (!socket_) {
        return false;
    }

    return socket_->send(message).success;
}

void MessagePortClient::Stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        isRunning_ = false;
    }

    if (socket_) {
        socket_->stop();
    }
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

bool MessagePortClient::IsRunning() {
    std::lock_guard<std::mutex> lock(mtx_);
    return isRunning_;
}
