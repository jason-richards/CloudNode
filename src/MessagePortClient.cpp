#include "MessagePortClient.hpp"
#include "Logger.hpp"

/** Registers the callback invoked when a message is received. */
void MessagePortClient::OnMessage(MessageCallback callback) {
    m_messageCallback = std::move(callback);
}

/** Registers the callback invoked when the WebSocket connection closes. */
void MessagePortClient::OnClose(CloseCallback callback) {
    m_closeCallback = std::move(callback);
}

/** Registers the callback invoked after the WebSocket connection opens. */
void MessagePortClient::OnOpen(OpenCallback callback) {
    m_openCallback = std::move(callback);
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
    std::unique_lock<std::mutex> lock(m_mtx);
    if (m_isRunning) {
        return false;
    }

    m_socket = std::make_unique<ix::WebSocket>();
    m_socket->setUrl(address + ":" + std::to_string(port));

    ix::WebSocketHttpHeaders headers;
    headers["x-client-id"] = m_name;
    m_socket->setExtraHeaders(headers);

    // Translate WebSocket events into MessagePortClient state and callbacks.
    m_socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        if (!msg) {
            return;
        }

        switch (msg->type) {
        case ix::WebSocketMessageType::Open:
            Logger::info() << "ix::WebSocketMessageType::Open";
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_isRunning = true;
            }
            if (m_openCallback) {
                m_openCallback(nullptr, m_name);
            }
            break;
        case ix::WebSocketMessageType::Message:
            if (m_messageCallback) {
                m_messageCallback(nullptr, msg->str);
            }
            break;
        case ix::WebSocketMessageType::Close:
            Logger::info() << "ix::WebSocketMessageType::Close";
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_isRunning = false;
            }
            if (m_closeCallback) {
                m_closeCallback(nullptr, msg->closeInfo.code, msg->closeInfo.reason);
            }
            break;
        case ix::WebSocketMessageType::Error:
            Logger::info() << "ix::WebSocketMessageType::Error";
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_isRunning = false;
            }
            break;
        };
    });

    m_serverThread = std::thread([this]() {
        m_socket->start();
    });

    return true;
}

/** Sends a text message through the active WebSocket connection. */
bool MessagePortClient::Send(const std::string& message) {
    if (!m_socket) {
        return false;
    }

    return m_socket->send(message).success;
}

/** Stops the WebSocket and waits for its background thread to finish. */
void MessagePortClient::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_isRunning = false;
    }

    if (m_socket) {
        m_socket->stop();
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

/** Returns whether the client has received an open event and remains active. */
bool MessagePortClient::IsRunning() {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_isRunning;
}
