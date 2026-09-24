#ifndef WEBRTC_HPP
#define WEBRTC_HPP

#include "MessagePort.hpp"

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
    enum class Type {
        Server,
        Client
    };

    virtual ~WebRTC() = default;

    static std::unique_ptr<WebRTC> Create(Type type);

    void MessageRouter(
        MessagePort::Ws * ws,
        const std::string& jsonStr
    ) {
        rapidjson::Document doc;
        doc.Parse(jsonStr.data(), jsonStr.size());

        if (doc.HasParseError() || !doc.HasMember("type") || !doc["type"].IsString()) {
            return;
        }

        std::string type = doc["type"].GetString();
        switch (string_hash(type)) {
            case string_hash("join"):
                Join(ws, doc);
                break;
            case string_hash("leave"):
                Leave(ws, doc);
                break;
            case string_hash("file_accept"):
                FileAccept(ws, doc);
                break;
            case string_hash("file_offer"):
                FileOffer(ws, doc);
                break;
            case string_hash("ping"):
                Ping(ws, doc);
                break;
        }
    }

    virtual void MessageOpen(MessagePort::Ws * ws, const std::string& user) = 0;
    virtual void MessageClose(MessagePort::Ws * ws, int code, std::string_view message) = 0;

protected:

    virtual void Join(MessagePort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void Leave(MessagePort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void FileAccept(MessagePort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void FileOffer(MessagePort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void Ping(MessagePort::Ws * ws, rapidjson::Document& doc) = 0;
};

#endif // WEBRTC_HPP
