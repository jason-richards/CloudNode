#ifndef WEBRTC_CLIENT_HPP
#define WEBRTC_CLIENT_HPP

#include "WebRTC.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

class WebRTCClient final : public WebRTC {
public:
    explicit WebRTCClient(const PropertyBag& properties)
        : WebRTC(MessagePort::Type::Client, properties) {}
    ~WebRTCClient() override;

    bool Start() override;
    void Stop() override;
    bool IsRunning() override;

    void OnMessageOpen(const std::string& peer) override;
    void OnMessageClose(int code, std::string_view message) override;

    bool JoinRoom(const std::string& room);
    bool SendPing();

    void
    SendFileOffer(
        const std::string&  room,
        const std::string&  peer,
        const std::string&  fileDetails
    );

protected:

    void OnJoin(rapidjson::Document& doc) override;
    void OnLeave(rapidjson::Document& doc) override;
    void OnFileAccept(rapidjson::Document& doc) override;
    void OnFileOffer(rapidjson::Document& doc) override;
    void OnPingPong(rapidjson::Document& doc) override;

private:
    enum class ClientState {
        CLIENT_INIT,
        CLIENT_WAITING_FOR_JOIN,
        CLIENT_JOINED,
        CLIENT_RUN,
        CLIENT_PING,
        CLIENT_WAIT_FOR_PONG,
        CLIENT_EXIT
    };

    void StartClient();

    std::mutex m_clientStateMutex;
    std::condition_variable m_clientStateChanged;
    std::thread m_clientThread;
    ClientState m_clientState{ClientState::CLIENT_INIT};
    bool m_transportOpen{false};
    std::string m_room;
    std::string m_peer;
    std::string m_file;
};

#endif // WEBRTC_CLIENT_HPP
