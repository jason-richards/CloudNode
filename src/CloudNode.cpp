#include "rtc/rtc.hpp"

#include <signal.h>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "parse_cl.h"
#include "RandomName.hpp"
#include "LocalDescription.hpp"
#include "WebRTC.hpp"
#include "WebRTCClient.hpp"
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



/**
 * @brief Gathers the initial RTCPeerConnection candidates.
 *
 * This function attempts to gather the initial ICE (Interactive Connectivity Establishment) candidates from a given
 * PeerConnection. The purpose is to collect all possible candidate addresses that can be used for establishing a 
 * connection between peers in a WebRTC session.
 *
 * @param pc A shared pointer to an rtc::PeerConnection object, representing the RTCPeerConnection instance.
 * @param desc A reference to a LocalDescription object, which will hold the gathered description after this
 * function completes.
 * @param time_out The maximum number of seconds to wait before timing out the operation. Defaults to 10 seconds if
 * not specified.
 *
 * @return bool True if the candidates were successfully gathered within the given timeout period; otherwise, false.
 *
 * @note This function should be called once an RTCPeerConnection has been created and configured appropriately.
 */
bool
obtainInitialDescription(
    std::shared_ptr<rtc::PeerConnection> pc,
    LocalDescription &desc,
    int time_out = 10
) {
    if (!pc) return false;

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    pc->onGatheringStateChange([&](rtc::PeerConnection::GatheringState state) {
        std::lock_guard<std::mutex> lock(mtx);
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            done = true;
            cv.notify_one();
        }
    });

    pc->onLocalDescription([&](rtc::Description description) {
        desc.descriptions.emplace_back(description);
    });

    pc->onLocalCandidate([&](rtc::Candidate candidate) {
        desc.candidates.emplace_back(candidate);
    });

    if (!pc->createDataChannel(std::string("Initial Description"))) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mtx);
    return cv.wait_for(
        lock,
        std::chrono::seconds(time_out),
        [&done] {
            return done;
        }
    );
}


int
main(
    int argc,
    char * argv[]
) {
    struct sigaction newHandler, oldHandler;

    memset(&newHandler, 0, sizeof newHandler);
    memset(&oldHandler, 0, sizeof oldHandler);
    newHandler.sa_sigaction = signalHandler;
    newHandler.sa_flags = SA_SIGINFO;

    sigaction(SIGINT, &newHandler, &oldHandler);

    g_stopToken = CreateStopToken();
    Start(g_stopToken);

    rtc::Configuration config;
    Cmdline params(argc, argv);

    std::string stunServer;
    if (params.stunAddress().substr(0, 5).compare("stun:") != 0) {
        stunServer = "stun:";
    }

    stunServer += params.stunAddress() + ":" + std::to_string(params.stunPort());
    config.iceServers.emplace_back(stunServer);

    std::string name;
    if (params.n().length()) {
        name = params.n();
    } else {
        name = RandomNameGenerator::Generate();
    }

    Logger::info() << "'" << name << "' is coming online.";

    bool isServer = params.messageAddress().empty();

    auto webRTC = WebRTC::Create(
        isServer ? WebRTC::Type::Server : WebRTC::Type::Client,
        name
    );

    Logger::info()
        << "Running in"
        << (isServer ? " SERVER " : " CLIENT ")
        << "mode.";

    if (webRTC->Start(params.messageAddress(), params.messagePort())) {
        if (auto clientRTC = dynamic_cast<WebRTCClient*>(webRTC.get())) {
            clientRTC->JoinRoom(params.r());
        }
        while (!StopRequested(g_stopToken)) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        webRTC->Stop();
    }

    return 0;
}

