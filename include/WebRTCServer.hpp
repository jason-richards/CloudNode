#ifndef WEBRTC_SERVER_HPP
#define WEBRTC_SERVER_HPP

#include "WebRTC.hpp"

#include <map>
#include <string>
#include <vector>

class WebRTCServer final : public WebRTC {
public:
    void MessageOpen(MessagePort::Ws * ws, const std::string& user) override;
    void MessageClose(MessagePort::Ws * ws, int code, std::string_view message) override;

protected:

    void Join(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void Leave(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void FileAccept(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void FileOffer(MessagePort::Ws * ws, rapidjson::Document& doc) override;
    void Ping(MessagePort::Ws * ws, rapidjson::Document& doc) override;

private:
    struct FileDetails {
        std::string name;
        size_t size;
        std::string mimeType;
    };

    void SendJoinNotification(const std::string& whoToNotify, const std::string& whoJoined);
    void SendLeaveNotification(const std::string& whoToNotify, const std::string& whoLeft);
    void Leave(MessagePort::Ws * ws, const std::string& user, const std::string& room);
    void HandleFile(MessagePort::Ws * ws, rapidjson::Document& doc, FileDetails& file);

    std::map<std::string, MessagePort::Ws*> clientDB_;
    std::map<std::string, std::string> pendingOffers_;
    std::map<std::string, std::vector<std::string>> rooms_;
};

#endif // WEBRTC_SERVER_HPP
