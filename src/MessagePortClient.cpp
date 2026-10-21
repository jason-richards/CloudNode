#include "MessagePortClient.hpp"
#include "Logger.hpp"

#include <algorithm>

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

    const std::string url = address + ":" + std::to_string(port);

    m_serverThread = std::thread([this, url]() {
        // A fresh socket per attempt avoids stale transport state after a server restart.
        const auto delay = std::max(m_reconnectDelay, std::chrono::seconds(1));
        while (true) {
            auto socket = MakeSocket(url);
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                if (m_stopRequested) {
                    break;
                }
                m_disconnected = false;
                m_socket = std::move(socket);
            }
            m_socket->start();

            std::unique_lock<std::mutex> lock(m_mtx);
            m_runningChanged.wait(lock, [this]() { return m_disconnected || m_stopRequested; });
            lock.unlock();
            m_socket->stop();
            lock.lock();
            if (m_stopRequested) {
                break;
            }
            m_runningChanged.wait_for(lock, delay, [this]() { return m_stopRequested; });
            if (m_stopRequested) {
                break;
            }
        }
    });

    return true;
}

std::unique_ptr<ix::WebSocket> MessagePortClient::MakeSocket(const std::string& url) {
    auto socket = std::make_unique<ix::WebSocket>();
    socket->setUrl(url);
    socket->disableAutomaticReconnection();

    ix::WebSocketHttpHeaders headers;
    headers["x-client-id"] = m_name;
    socket->setExtraHeaders(headers);

    // Translate WebSocket events into MessagePortClient state and callbacks.
    socket->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
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
            Logger::info() << "ix::WebSocketMessageType::Close " << msg->closeInfo.code << " '" << msg->closeInfo.reason << "'";
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_isRunning = false;
                m_disconnected = true;
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
                m_disconnected = true;
                m_isRunning = false;
            }
            m_runningChanged.notify_all();
            break;
        };
    });

    return socket;
}

void MessagePortClient::SetReconnectDelay(std::chrono::seconds delay) {
    m_reconnectDelay = delay;
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

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

bool MessagePortClient::IsRunning() {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_isRunning;
}
