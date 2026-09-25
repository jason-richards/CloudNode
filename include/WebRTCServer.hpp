#ifndef WEBRTC_SERVER_HPP
#define WEBRTC_SERVER_HPP

#include "WebRTC.hpp"

#include <map>
#include <string>
#include <vector>

class WebRTCServer final : public WebRTC {
public:
    explicit WebRTCServer(const PropertyBag& properties)
        : WebRTC(MessagePort::Type::Server, properties) {}

    void OnMessageOpen(const std::string& user) override;
    void OnMessageClose(int code, std::string_view message) override;

protected:

    void OnJoin(rapidjson::Document& doc) override;
    void OnLeave(rapidjson::Document& doc) override;
    void OnFileAccept(rapidjson::Document& doc) override;
    void OnFileOffer(rapidjson::Document& doc) override;
    void OnPingPong(rapidjson::Document& doc) override;

private:
    struct FileDetails {
        std::string name;
        size_t size;
        std::string mimeType;
    };

    void SendJoinNotification(const std::string& whoToNotify, const std::string& whoJoined);
    void SendLeaveNotification(const std::string& whoToNotify, const std::string& whoLeft);
    void Leave(const std::string& user, const std::string& room);
    void HandleFile(rapidjson::Document& doc, FileDetails& file);

    std::map<std::string, MessagePort::Ws*> clientDB_;
    std::map<std::string, std::string> pendingOffers_;
    std::map<std::string, std::vector<std::string>> rooms_;
};

#endif // WEBRTC_SERVER_HPP
