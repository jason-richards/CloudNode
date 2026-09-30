#ifndef CONTROL_PORT_SERVER_HPP
#define CONTROL_PORT_SERVER_HPP

#include "MessagePort.hpp"

#include <map>

class MessagePortServer final : public MessagePort {
public:
    explicit MessagePortServer(const std::string& name) : MessagePort(name) {}

    ~MessagePortServer() override;
    void OnMessage(MessageCallback callback) override;
    void OnClose(CloseCallback callback) override;
    void OnOpen(OpenCallback callback) override;
    bool Start(const std::string& address, int port) override;
    bool Send(const std::string& message) override;
    void Stop() override;
    bool IsRunning() override;

private:
    std::map<std::string, Ws*> clients;
    uWS::Loop* m_loop{nullptr};
    us_listen_socket_t* m_listenSocket{nullptr};
};

#endif // CONTROL_PORT_SERVER_HPP
