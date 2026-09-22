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
 * @brief Generates a random Universally Unique Identifier (UUID) Version 4.
 *
 * This function creates a random 128-bit number compliant with RFC 4122, Version 4.
 * It uses the system's entropy source to generate entropy and then formats the
 * resulting bytes into a standard string representation: 8-4-4-4-12.
 *
 * The UUID is mathematically unique with an estimated probability of collision
 * being approximately $1 \text{ in } 2^{122}$.
 *
 * @return std::string A new UUID string formatted as: 
 *                     8 hexadecimal digits-4 hexadecimal digits-4 hexadecimal digits-4 hexadecimal digits-12 hexadecimal digits.
 *
 * @example
 *   std::string id = generateUUID();
 *   std::cout << "ID: " << id << std::endl;
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


/**
 * @brief Encodes a string into Base64 format.
 *
 * @param input The raw string data to be encoded.
 * @param encoded The output string where the Base64 encoded data will be stored.
 * @return true Returns true if the input is non-empty and encoding completes successfully.
 * @return false Returns false if the input string is empty.
 *
 * @details
 * This function uses the `base64::encoder` utility. It pre-allocates the memory for the
 * output string based on an estimated size (input length * 4) to improve performance.
 * The final output has any newline characters stripped out before returning.
 */
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


/**
 * @brief Decodes a Base64 encoded string back into its original form.
 *
 * @param input The Base64 encoded string to be decoded.
 * @param decoded The output string where the original decoded data will be stored.
 * @return true Returns true if the input is non-empty and decoding completes successfully.
 * @return false Returns false if the input string is empty.
 */
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

