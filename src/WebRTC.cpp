#include "WebRTC.hpp"

#include "WebRTCClient.hpp"
#include "WebRTCServer.hpp"

std::unique_ptr<WebRTC>
WebRTC::Create(Type type, const std::string& name) {
    if (type == Type::Server) {
        return std::make_unique<WebRTCServer>(name);
    }

    return std::make_unique<WebRTCClient>(name);
}

WebRTC::WebRTC(MessagePort::Type type, const std::string& name)
    : messagePort_(MessagePort::Create(type, name)) {
    messagePort_->OnOpen([this](MessagePort::Ws* ws, const std::string& user) {
        currentWebSocket_ = ws;
        OnMessageOpen(user);
    });
    messagePort_->OnClose([this](MessagePort::Ws* ws, int code, std::string_view message) {
        currentWebSocket_ = ws;
        OnMessageClose(code, message);
    });
    messagePort_->OnMessage([this](MessagePort::Ws* ws, const std::string& message) {
        currentWebSocket_ = ws;
        MessageRouter(message);
    });
}

bool WebRTC::Start(const std::string& address, int port) {
    return messagePort_->Start(address, port);
}

void WebRTC::Stop() {
    messagePort_->Stop();
}

bool WebRTC::Wait() {
    return messagePort_->Wait();
}

bool WebRTC::IsRunning() {
    return messagePort_->IsRunning();
}