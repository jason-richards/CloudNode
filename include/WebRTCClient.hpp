#ifndef WEBRTC_CLIENT_HPP
#define WEBRTC_CLIENT_HPP

#include "WebRTC.hpp"

class WebRTCClient final : public WebRTC {
protected:
    void JoinCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void LeaveCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void FileAcceptCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void FileOfferCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void PingCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
};

#endif // WEBRTC_CLIENT_HPP