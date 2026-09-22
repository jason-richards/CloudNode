#ifndef ADMIN_PORT_H
#define ADMIN_PORT_H

#include <App.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>


class ControlPort {
    struct PerSocketData {
        std::string x_client_id;
        /* User data attached to each connection if needed */
    };
    std::map<std::string, uWS::WebSocket<false, true, PerSocketData>*> clients;
public:
    using Ws = uWS::WebSocket<false, true, PerSocketData>;
    using MessageCallback = std::function<void(Ws*, const std::string&)>;
    using CloseCallback = std::function<void(Ws* ws, int code, std::string_view message)>;

    ControlPort();
    ~ControlPort();

    // Prevent copying
    ControlPort(const ControlPort&) = delete;
    ControlPort& operator=(const ControlPort&) = delete;

    // Register callback triggered when a string message is received
    void OnMessage(MessageCallback callback);
    void OnClose(CloseCallback callback);
    void OnOpen(MessageCallback callback);

    // Asynchronously start the WebSocket server on the specified port
    bool Start(int port);

    // Stop the server and join the background thread
    void Stop();


    bool Wait();
    bool IsRunning();

private:

    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    MessageCallback openCallback_;

    std::thread serverThread_;
    std::atomic<bool> isRunning_{false};
    
    uWS::Loop* loop_{nullptr};
    us_listen_socket_t* listenSocket_{nullptr};

    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif // ADMIN_PORT_H
