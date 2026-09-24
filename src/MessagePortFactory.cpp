#include "MessagePort.hpp"

#include "MessagePortClient.hpp"
#include "MessagePortServer.hpp"

std::unique_ptr<MessagePort>
MessagePort::Create(Type type) {
    if (type == Type::Server) {
        return std::make_unique<MessagePortServer>();
    }

    return std::make_unique<MessagePortClient>();
}
