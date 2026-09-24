#ifndef CONTROL_PORT_HPP
#define CONTROL_PORT_HPP

#include <App.h>
#include <atomic>
#include <condition_variable>
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
    using MessageCallback = std::function<void(Ws*, const std::string&)>;
    using CloseCallback = std::function<void(Ws*, int, std::string_view)>;

    enum class Type {
        Server,
        Client
    };

    virtual ~MessagePort() = default;

    MessagePort(const MessagePort&) = delete;
    MessagePort& operator=(const MessagePort&) = delete;

    static std::unique_ptr<MessagePort> Create(Type type);

    virtual void OnMessage(MessageCallback callback) = 0;
    virtual void OnClose(CloseCallback callback) = 0;
    virtual void OnOpen(MessageCallback callback) = 0;
    virtual bool Start(int port) = 0;
    virtual void Stop() = 0;
    virtual bool Wait() = 0;
    virtual bool IsRunning() = 0;

protected:
    MessagePort() = default;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    MessageCallback openCallback_;
    std::thread serverThread_;
    std::atomic<bool> isRunning_{false};
    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif // CONTROL_PORT_HPP
