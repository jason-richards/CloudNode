#ifndef CONTROL_PORT_CLIENT_HPP
#define CONTROL_PORT_CLIENT_HPP

#include "MessagePort.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <memory>

class MessagePortClient final : public MessagePort {
public:
    explicit MessagePortClient(const std::string& name) : MessagePort(name) {}

    void OnMessage(MessageCallback callback) override;
    void OnClose(CloseCallback callback) override;
    void OnOpen(OpenCallback callback) override;
    bool Start(const std::string& address, int port) override;
    bool Send(const std::string& message) override;
    void Stop() override;
    bool IsRunning() override;

private:
    std::unique_ptr<ix::WebSocket> socket_;
};

#endif // CONTROL_PORT_CLIENT_HPP
