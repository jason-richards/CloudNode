#include "MessagePortClient.hpp"
#include "Logger.hpp"

void MessagePortClient::OnMessage(MessageCallback callback) {
    m_messageCallback = std::move(callback);
}

void MessagePortClient::OnClose(CloseCallback callback) {
    m_closeCallback = std::move(callback);
}

void MessagePortClient::OnOpen(OpenCallback callback) {
    m_openCallback = std::move(callback);
}

bool MessagePortClient::Start(const std::string& address, int port) {
    std::unique_lock<std::mutex> lock(m_mtx);
    if (m_isRunning) {
        return false;
    }
    m_stopRequested = false;

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
                m_isRunning = !m_stopRequested;
            }
            m_runningChanged.notify_all();
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
            m_runningChanged.notify_all();
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
            m_runningChanged.notify_all();
            break;
        };
    });

    m_serverThread = std::thread([this]() {
        m_socket->start();
    });

    return true;
}

bool MessagePortClient::Send(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_socket || !m_isRunning) {
        return false;
    }

    return m_socket->send(message).success;
}

void MessagePortClient::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_isRunning = false;
        m_stopRequested = true;
    }
    m_runningChanged.notify_all();

    if (m_socket) {
        m_socket->stop();
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

bool MessagePortClient::IsRunning() {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_isRunning;
}
