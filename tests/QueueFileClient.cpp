// Mirrors src/CloudNode.cpp, but instead of sending --file at startup it queues
// that file through WebRTCClient::QueueFile after waiting 10 seconds.

#include <signal.h>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "parse_cl.h"
#include "RandomName.hpp"
#include "WebRTCClient.hpp"
#include "Logger.hpp"
#include "StopToken.hpp"

namespace {

StopTokenPtr g_stopToken;

void
signalHandler(int signalNum, siginfo_t*, void*) {
    if (signalNum == SIGINT) {
        RequestStop(g_stopToken);
    }
}

// Splits "peer:room:path"; only the first two colons delimit.
bool
ParseSpec(const std::string& text, FileSpec& spec) {
    const auto first = text.find(':');
    const auto second = first == std::string::npos ? first : text.find(':', first + 1);
    if (second == std::string::npos) {
        return false;
    }
    spec = FileSpec{text.substr(0, first), text.substr(first + 1, second - first - 1),
                    text.substr(second + 1)};
    return !spec.peer.empty() && !spec.room.empty() && !spec.path.empty();
}

}  // namespace

int
main(int argc, char* argv[]) {
    struct sigaction newHandler, oldHandler;

    g_stopToken = CreateStopToken();
    Start(g_stopToken);

    memset(&newHandler, 0, sizeof newHandler);
    memset(&oldHandler, 0, sizeof oldHandler);
    newHandler.sa_sigaction = signalHandler;
    newHandler.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &newHandler, &oldHandler);

    Cmdline params(argc, argv);

    FileSpec spec;
    if (!ParseSpec(params.file(), spec)) {
        Logger::error() << "--file expects peer:room:path.";
        return 1;
    }

    const std::string name = params.name().length() ? params.name() : RandomNameGenerator::Generate();

    PropertyBag properties;
    properties.Set("messageAddress",    params.messageAddress());
    properties.Set("messagePort",       params.messagePort());
    properties.Set("stunAddress",       params.stunAddress());
    properties.Set("stunPort",          params.stunPort());
    properties.Set("server",            params.server());
    properties.Set("directory",         params.directory());
    properties.Set("room",              params.room());
    properties.Set("file",              std::string());  // queued later instead of sent at startup
    properties.Set("help",              params.help());
    properties.Set("version",           params.version());
    properties.Set("name",              name);
    properties.Set("timeout",           params.timeout());
    properties.Set("maxMessageSize",    params.maxMessageSize());

    Logger::info() << "'" << name << "' is coming online.";

    auto webRTC = WebRTC::Create(properties);
    auto* client = dynamic_cast<WebRTCClient*>(webRTC.get());
    if (!client) {
        Logger::error() << "QueueFileClient must run in client mode.";
        return 1;
    }

    if (webRTC->Start()) {
        const auto queueTime = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool queued = false;
        while (!StopRequested(g_stopToken) && webRTC->IsRunning()) {
            if (!queued && std::chrono::steady_clock::now() >= queueTime) {
                queued = true;
                if (client->QueueFile(spec.peer, spec.room, spec.path)) {
                    Logger::info() << "Queued file '" << spec.path << "' for '" << spec.peer << "'.";
                } else {
                    Logger::error() << "QueueFile failed.";
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        webRTC->Stop();
    }

    return 0;
}
