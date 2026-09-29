#ifndef WEBRTC_CLIENT_HPP
#define WEBRTC_CLIENT_HPP

#include "WebRTC.hpp"

class WebRTCClient final : public WebRTC {
public:
    explicit WebRTCClient(const PropertyBag& properties)
        : WebRTC(MessagePort::Type::Client, properties) {}

    void OnMessageOpen(const std::string& peer) override;
    void OnMessageClose(int code, std::string_view message) override;

    void JoinRoom(const std::string& room);
    void SendPing();

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
};

#endif // WEBRTC_CLIENT_HPP
