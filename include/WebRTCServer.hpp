#ifndef WEBRTC_SERVER_HPP
#define WEBRTC_SERVER_HPP

#include "WebRTC.hpp"

#include <map>
#include <string>
#include <vector>

class WebRTCServer final : public WebRTC {
public:
    void ControlOpen(ControlPort::Ws * ws, const std::string& user);
    void ControlClose(ControlPort::Ws * ws, int code, std::string_view message);

protected:
    void JoinCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void LeaveCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void FileAcceptCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void FileOfferCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;
    void PingCommand(ControlPort::Ws * ws, rapidjson::Document& doc) override;

private:
    struct FileDetails {
        std::string name;
        size_t size;
        std::string mimeType;
    };

    void SendJoinNotification(const std::string& whoToNotify, const std::string& whoJoined);
    void SendLeaveNotification(const std::string& whoToNotify, const std::string& whoLeft);
    void LeaveCommand(ControlPort::Ws * ws, const std::string& user, const std::string& room);
    void HandleFileCommand(ControlPort::Ws * ws, rapidjson::Document& doc, FileDetails& file);

    std::map<std::string, ControlPort::Ws*> clientDB_;
    std::map<std::string, std::string> pendingOffers_;
    std::map<std::string, std::vector<std::string>> rooms_;
};

#endif // WEBRTC_SERVER_HPP