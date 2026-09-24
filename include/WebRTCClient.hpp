#ifndef WEBRTC_CLIENT_HPP
#define WEBRTC_CLIENT_HPP

#include "WebRTC.hpp"

class WebRTCClient final : public WebRTC {
public:
    void MessageOpen(MessagePort::Ws * ws, const std::string& user) override;
    void MessageClose(MessagePort::Ws * ws, int code, std::string_view message) override;

protected:

    void Join(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void Leave(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void FileAccept(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void FileOffer(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void Ping(MessagePort::Ws * ws, rapidjson::Document& doc) override;
};

#endif // WEBRTC_CLIENT_HPP
