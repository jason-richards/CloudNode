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
    bool WaitUntilRunning() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] {
            return isRunning_.load() || startupFailed_.load();
        });
        return isRunning_.load();
    }
    virtual void Stop() = 0;
    virtual bool Wait() = 0;
    virtual bool IsRunning() = 0;

protected:
    MessagePort(const std::string& name) : name_(name) {};
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    OpenCallback openCallback_;
    std::thread serverThread_;
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> startupFailed_{false};
    std::mutex mtx_;
    std::condition_variable cv_;
    std::string name_;
};

#endif // CONTROL_PORT_HPP
