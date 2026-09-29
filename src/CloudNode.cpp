
#include <signal.h>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "parse_cl.h"
#include "RandomName.hpp"
#include "LocalDescription.hpp"
#include "WebRTCServer.hpp"
#include "Logger.hpp"
#include "StopToken.hpp"

StopTokenPtr g_stopToken;


void
signalHandler(
    int signalNum,
    siginfo_t * info,
    void * context
) {
    (void) info;
    (void) context;
    switch (signalNum) {
    case SIGINT:
        RequestStop(g_stopToken);
    default:
        return;
    };
}



int
main(
    int argc,
    char * argv[]
) {
    struct sigaction newHandler, oldHandler;

    g_stopToken = CreateStopToken();
    Start(g_stopToken);

    memset(&newHandler, 0, sizeof newHandler);
    memset(&oldHandler, 0, sizeof oldHandler);
    newHandler.sa_sigaction = signalHandler;
    newHandler.sa_flags = SA_SIGINFO;

    sigaction(SIGINT, &newHandler, &oldHandler);

    //rtc::Configuration config;
    Cmdline params(argc, argv);

    //std::string stunServer;
    //if (params.stunAddress().substr(0, 5).compare("stun:") != 0) {
    //    stunServer = "stun:";
    //}

    //stunServer += params.stunAddress() + ":" + std::to_string(params.stunPort());
    //config.iceServers.emplace_back(stunServer);

    std::string name;
    if (params.name().length()) {
        name = params.name();
    } else {
        name = RandomNameGenerator::Generate();
    }

    PropertyBag properties;
    properties.Set("messageAddress", params.messageAddress());
    properties.Set("messagePort", params.messagePort());
    properties.Set("stunAddress", params.stunAddress());
    properties.Set("stunPort", params.stunPort());
    properties.Set("server", params.server());
    properties.Set("directory", params.directory());
    properties.Set("room", params.room());
    properties.Set("peer", params.peer());
    properties.Set("file", params.file());
    properties.Set("help", params.help());
    properties.Set("version", params.version());
    properties.Set("name", name);

    Logger::info() << "'" << name << "' is coming online.";

    auto webRTC = WebRTC::Create(properties);

    bool isServer = dynamic_cast<WebRTCServer*>(webRTC.get()) != nullptr ? true : false;

    Logger::info()
        << "Running in"
        << (isServer ? " SERVER " : " CLIENT ")
        << "mode.";

    if (webRTC->Start()) {
        while (!StopRequested(g_stopToken)) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        webRTC->Stop();
    }

    return 0;
}

