#ifndef WEBRTC_HPP
#define WEBRTC_HPP

#include "ControlPort.hpp"

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

    void ControlCommandRouter(
        ControlPort::Ws * ws,
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
                JoinCommand(ws, doc);
                break;
            case string_hash("leave"):
                LeaveCommand(ws, doc);
                break;
            case string_hash("file_accept"):
                FileAcceptCommand(ws, doc);
                break;
            case string_hash("file_offer"):
                FileOfferCommand(ws, doc);
                break;
            case string_hash("ping"):
                PingCommand(ws, doc);
                break;
        }
    }

protected:
    virtual void JoinCommand(ControlPort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void LeaveCommand(ControlPort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void FileAcceptCommand(ControlPort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void FileOfferCommand(ControlPort::Ws * ws, rapidjson::Document& doc) = 0;
    virtual void PingCommand(ControlPort::Ws * ws, rapidjson::Document& doc) = 0;
};

#endif // WEBRTC_HPP