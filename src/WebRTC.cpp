#include "WebRTC.hpp"

#include "WebRTCClient.hpp"
#include "WebRTCServer.hpp"

std::unique_ptr<WebRTC>
WebRTC::Create(Type type) {
    if (type == Type::Server) {
        return std::make_unique<WebRTCServer>();
    }

    return std::make_unique<WebRTCClient>();
}