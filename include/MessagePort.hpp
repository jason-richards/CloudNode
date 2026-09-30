#ifndef CONTROL_PORT_HPP
#define CONTROL_PORT_HPP

#include <App.h>
#include <atomic>
#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <string_view>

class MessagePort {
public:
    struct PerSocketData {
        std::string x_client_id;
    };

    using Ws = uWS::WebSocket<false, true, PerSocketData>;
    using MessageCallback = std::function<void(Ws* /*ws*/, const std::string&)>;
    using OpenCallback = std::function<void(Ws* /*ws*/, const std::string&)>;
    using CloseCallback = std::function<void(Ws*, int, std::string_view)>;

    enum class Type {
        Server,
        Client
    };

    virtual ~MessagePort() = default;

    MessagePort(const MessagePort&) = delete;
    MessagePort& operator=(const MessagePort&) = delete;

    static std::unique_ptr<MessagePort> Create(Type type, const std::string& name);

    virtual void OnMessage(MessageCallback callback) = 0;
    virtual void OnClose(CloseCallback callback) = 0;
    virtual void OnOpen(OpenCallback callback) = 0;
    virtual bool Start(const std::string& address, int port) = 0;
    virtual bool Send(const std::string& message) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() = 0;

protected:
    MessagePort(const std::string& name) : m_name(name) {};
    MessageCallback m_messageCallback;
    CloseCallback m_closeCallback;
    OpenCallback m_openCallback;
    std::thread m_serverThread;
    std::atomic<bool> m_isRunning{false};
    std::mutex m_mtx;
    std::string m_name;
};

#endif // CONTROL_PORT_HPP
