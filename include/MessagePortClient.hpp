#ifndef CONTROL_PORT_CLIENT_HPP
#define CONTROL_PORT_CLIENT_HPP

#include "MessagePort.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <memory>

class MessagePortClient final : public MessagePort {
public:
    void OnMessage(MessageCallback callback) override;
    void OnClose(CloseCallback callback) override;
    void OnOpen(MessageCallback callback) override;
    bool Start(int port) override;
    void Stop() override;
    bool Wait() override;
    bool IsRunning() override;

private:
    std::unique_ptr<ix::WebSocket> socket_;
};

#endif // CONTROL_PORT_CLIENT_HPP
