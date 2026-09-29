#include "MessagePortClient.hpp"
#include "Logger.hpp"

/** Registers the callback invoked when a message is received. */
void MessagePortClient::OnMessage(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

/** Registers the callback invoked when the WebSocket connection closes. */
void MessagePortClient::OnClose(CloseCallback callback) {
    closeCallback_ = std::move(callback);
}

/** Registers the callback invoked after the WebSocket connection opens. */
void MessagePortClient::OnOpen(OpenCallback callback) {
    openCallback_ = std::move(callback);
}

/**
 * Creates and starts the WebSocket client.
 *
 * The client identifies itself with the configured name in the
 * `x-client-id` request header. WebSocket events are translated into the
 * registered callbacks, and the socket's event loop runs on a background
 * thread.
 *
 * @return false when the client is already running; true when startup begins.
 */
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

    // Translate WebSocket events into MessagePortClient state and callbacks.
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

/** Sends a text message through the active WebSocket connection. */
bool MessagePortClient::Send(const std::string& message) {
    if (!socket_) {
        return false;
    }

    return socket_->send(message).success;
}

/** Stops the WebSocket and waits for its background thread to finish. */
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

/** Returns whether the client has received an open event and remains active. */
bool MessagePortClient::IsRunning() {
    std::lock_guard<std::mutex> lock(mtx_);
    return isRunning_;
}
