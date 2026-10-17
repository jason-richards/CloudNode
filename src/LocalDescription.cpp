#include <rapidjson/document.h>
#include <rapidjson/error/error.h>

#include <cstring>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>

#include "b64/encode.h"
#include "b64/decode.h"

#include "LocalDescription.hpp"

/**
 * @file LocalDescription.cpp
 * @brief Implements the WebRTC SDP/candidate representation used by CloudNode.
 *
 * This file centralizes creation and serialization of local peer descriptions,
 * including generation of a unique identifier, Base64 encoding for JSON-safe
 * transport, and the helper that gathers initial ICE candidates from a
 * `rtc::PeerConnection`.
 */

std::string
generateUUID() {
    // 1. Create a random number generator using the entropy of the OS
    std::random_device rd; 
    std::mt19937_64 gen(rd());

    // 2. Define distribution for random bytes (0-255)
    std::uniform_int_distribution<unsigned char> dis(0, 255);

    // 3. Generate 16 random bytes
    std::vector<unsigned char> bytes(16);
    for (int i = 0; i < 16; ++i) {
        bytes[i] = dis(gen);
    }

    // 4. Set the UUID Version and Variant bits (RFC 4122)
    // Version bit (4xxxxxxx) for bytes 6 and 7
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    // Variant bit (10xxxxxx) for byte 8
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    // 5. Format into the string: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::stringstream ss;
    ss << std::hex << std::uppercase;

    for (int i = 0; i < 16; ++i) {
        // Insert hyphens at the specific positions
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            ss << '-';
        }
        // Pad with leading zero and output
        ss << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }

    return ss.str();
}


bool
base64Encode(
    const std::string& input,
    std::string& encoded
) {
    if (input.empty()) return false;

    base64::encoder b64enc;
    std::istringstream input_stream(input);
    std::ostringstream output_stream;

    b64enc.encode(input_stream, output_stream);

    encoded = output_stream.str();
    encoded.erase(
        std::remove_if(
            encoded.begin(),
            encoded.end(), 
            [](unsigned char c) {
                return c == '\n' || c == '\r';
            }
        ), 
        encoded.end()
    );

    return true;
}


bool
base64Decode(
    const std::string &input,
    std::string& decoded
) {
    if (input.empty()) return false;

    base64::decoder b64dec;
    decoded.resize(strlen(input.c_str()));
    int s = b64dec.decode(
        reinterpret_cast<const char*>(input.c_str()),
        input.length(),
        decoded.data()
    );

    decoded.resize(s);

    return true;
}


LocalDescription::LocalDescription() : UUID(generateUUID()) {};

LocalDescription::LocalDescription(
    const LocalDescription& other
) {
    this->UUID = other.UUID;
    this->descriptions = other.descriptions;
    this->candidates = other.candidates;
}

LocalDescription::LocalDescription(
    const std::string& jsonStr
) {
    rapidjson::Document doc;
    doc.Parse(jsonStr.data(), jsonStr.size());

    if (doc.HasParseError()) {
        return; 
    }

    if (doc.HasMember("UUID") && doc["UUID"].IsString()) {
        UUID = doc["UUID"].GetString();
    }

    auto processArrays = [&](const char * name, std::vector<std::string>& target) {
        if (doc.HasMember(name) && doc[name].IsArray()) {
            for (rapidjson::SizeType i = 0; i < doc[name].Size(); i++) {
                if (doc[name][i].IsString()) {
                    std::string decoded_string;
                    if (base64Decode(doc[name][i].GetString(), decoded_string)) {
                        target.push_back(decoded_string);
                    }
                }
            }
        }
    };

    processArrays("descriptions", descriptions);
    processArrays("candidates",   candidates);
}

std::string
LocalDescription::GetDescriptionsJson() const {
    std::ostringstream oss;

    oss << "\"descriptions\" :[";
    for (size_t i = 0; i < descriptions.size(); ++i) {
        if (i > 0) oss << ","; // Add comma between items
        
        std::string encoded;
        if (base64Encode(descriptions[i], encoded)) {
            oss << "\"" << encoded << "\"";
        }
    }
    oss << "]";

    return oss.str();
}

std::string
LocalDescription::GetCandidatesJson() const {
    std::ostringstream oss;

    oss << "\"candidates\" :[";
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i > 0) oss << ","; // Add comma between items
        
        std::string encoded;
        if (base64Encode(candidates[i], encoded)) {
            oss << "\"" << encoded << "\"";
        }
    }
    oss << "]";

    return oss.str();
}

std::string
LocalDescription::toJson() const {
    std::ostringstream oss;

    oss << "{\"UUID\":\"" << UUID << "\"";

    auto appendArray = [&](const std::string& name, const std::vector<std::string>& items) {
        oss << "," << name << ":[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) oss << ","; // Add comma between items
            
            std::string encoded;
            if (base64Encode(items[i], encoded)) {
                oss << "\"" << encoded << "\"";
            }
        }
        oss << "]";
    };

    appendArray("\"descriptions\"", descriptions);
    appendArray("\"candidates\"", candidates);

    oss << "}";

    return oss.str();
}

bool
obtainInitialDescription(
    DataChannel& dataChannel,
    LocalDescription &desc,
    int time_out
) {
    if (!dataChannel.pc) return false;

    struct GatheredDescription {
        std::mutex mutex;
        std::condition_variable changed;
        bool complete{false};
        LocalDescription description;
    };
    auto gathered = std::make_shared<GatheredDescription>();

    dataChannel.pc->onGatheringStateChange([gathered](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            {
                std::lock_guard<std::mutex> lock(gathered->mutex);
                gathered->complete = true;
            }
            gathered->changed.notify_one();
        }
    });

    dataChannel.pc->onLocalDescription([gathered](rtc::Description description) {
        std::lock_guard<std::mutex> lock(gathered->mutex);
        gathered->description.descriptions.emplace_back(std::string(description));
    });

    dataChannel.pc->onLocalCandidate([gathered](rtc::Candidate candidate) {
        std::lock_guard<std::mutex> lock(gathered->mutex);
        gathered->description.candidates.emplace_back(std::string(candidate));
    });

    dataChannel.channel = dataChannel.pc->createDataChannel("File Transfer");
    if (!dataChannel.channel) {
        return false;
    }

    std::unique_lock<std::mutex> lock(gathered->mutex);
    if (!gathered->changed.wait_for(
        lock,
        std::chrono::seconds(time_out),
        [&gathered] { return gathered->complete; })) {
        return false;
    }

    desc = gathered->description;

    if (desc.descriptions.empty()) {
        return false;
    }

    dataChannel.fileQ = CreateStringQueue();
    return static_cast<bool>(dataChannel.fileQ);
}

