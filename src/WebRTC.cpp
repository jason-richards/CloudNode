#include "WebRTC.hpp"

#include "WebRTCClient.hpp"
#include "WebRTCServer.hpp"

std::unique_ptr<WebRTC>
WebRTC::Create(const PropertyBag& properties) {
    if (properties.Get<bool>("server")) {
        return std::make_unique<WebRTCServer>(properties);
    }

    return std::make_unique<WebRTCClient>(properties);
}

WebRTC::WebRTC(MessagePort::Type type, const PropertyBag& properties)
    : properties_(properties),
      messagePort_(MessagePort::Create(type, properties_.Get<std::string>("name"))) {
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

bool WebRTC::Start() {
    return messagePort_->Start(
        properties_.Get<std::string>("messageAddress"),
        properties_.Get<int>("messagePort")
    );
}

void WebRTC::Stop() {
    messagePort_->Stop();
}

bool WebRTC::IsRunning() {
    return messagePort_->IsRunning();
}
