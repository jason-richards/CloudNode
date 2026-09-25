#ifndef WEBRTC_HPP
#define WEBRTC_HPP

#include "MessagePort.hpp"
#include "PropertyBag.hpp"

#include <rapidjson/document.h>

#include <memory>
#include <string>
#include <string_view>


class WebRTC {
private:
    static constexpr unsigned int string_hash(std::string_view str) {
        unsigned int hash = 2166136261u;
        for (char c : str) {
            hash ^= static_cast<unsigned int>(c);
            hash *= 16777619u;
        }
        return hash;
    }

public:

    virtual ~WebRTC() = default;

    static std::unique_ptr<WebRTC> Create(const PropertyBag& properties);

    bool Start();
    void Stop();
    bool IsRunning();

    void MessageRouter(const std::string& jsonStr) {
        rapidjson::Document doc;
        doc.Parse(jsonStr.data(), jsonStr.size());

        if (doc.HasParseError() || !doc.HasMember("type") || !doc["type"].IsString()) {
            return;
        }

        std::string type = doc["type"].GetString();
        switch (string_hash(type)) {
            case string_hash("join"):
            case string_hash("peer_joined"):
                OnJoin(doc);
                break;
            case string_hash("peer_left"):
            case string_hash("leave"):
                OnLeave(doc);
                break;
            case string_hash("file_accept"):
                OnFileAccept(doc);
                break;
            case string_hash("file_offer"):
                OnFileOffer(doc);
                break;
            case string_hash("ping"):
            case string_hash("pong"):
                OnPingPong(doc);
                break;
        }
    }

    virtual void OnMessageOpen(const std::string& user) = 0;
    virtual void OnMessageClose(int code, std::string_view message) = 0;

protected:

    WebRTC(MessagePort::Type type, const PropertyBag& properties);

    virtual void OnJoin(rapidjson::Document& doc) = 0;
    virtual void OnLeave(rapidjson::Document& doc) = 0;
    virtual void OnFileAccept(rapidjson::Document& doc) = 0;
    virtual void OnFileOffer(rapidjson::Document& doc) = 0;
    virtual void OnPingPong(rapidjson::Document& doc) = 0;

    bool SendMessage(const std::string& message) { return messagePort_->Send(message); }
    MessagePort::Ws* CurrentWebSocket() const { return currentWebSocket_; }
    const PropertyBag& Properties() const { return properties_; }

private:
    PropertyBag properties_;
    std::unique_ptr<MessagePort> messagePort_;
    MessagePort::Ws* currentWebSocket_{nullptr};
};

#endif // WEBRTC_HPP
