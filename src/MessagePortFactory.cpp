#include "MessagePort.hpp"

#include "MessagePortClient.hpp"
#include "MessagePortServer.hpp"

std::unique_ptr<MessagePort>
MessagePort::Create(Type type, const std::string& name) {
    if (type == Type::Server) {
        return std::make_unique<MessagePortServer>(name);
    }

    return std::make_unique<MessagePortClient>(name);
}
