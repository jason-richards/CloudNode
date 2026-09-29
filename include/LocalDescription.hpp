#pragma once
#include "rtc/rtc.hpp"

#include <memory>
#include <string>
#include <iostream>


typedef struct
LocalDescription{
    std::string UUID;
    std::vector<std::string> descriptions;
    std::vector<std::string> candidates;

    LocalDescription();
    LocalDescription(const LocalDescription& other);
    LocalDescription(const std::string& jsonStr);

    std::string
    toJson() const;

    std::string
    GetDescriptionsJson() const;

    std::string
    GetCandidatesJson() const;

    friend std::ostream& operator<<(std::ostream& os, const LocalDescription& obj);

} LocalDescription;

using LocalDescriptionPtr = std::shared_ptr<LocalDescription>;


inline std::ostream&
operator<<(
    std::ostream& os,
    const LocalDescription& obj
) {
    os << obj.toJson();
    return os;
}


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
generateUUID();


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
);


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
);



/**
 * @brief Gathers the initial RTCPeerConnection candidates.
 *
 * This function attempts to gather the initial ICE (Interactive Connectivity Establishment) candidates from a given
 * PeerConnection. The purpose is to collect all possible candidate addresses that can be used for establishing a 
 * connection between peers in a WebRTC session.
 *
 * @param pc A shared pointer to an rtc::PeerConnection object, representing the RTCPeerConnection instance.
 * @param desc A reference to a LocalDescription object, which will hold the gathered description after this
 * function completes.
 * @param time_out The maximum number of seconds to wait before timing out the operation. Defaults to 10 seconds if
 * not specified.
 *
 * @return bool True if the candidates were successfully gathered within the given timeout period; otherwise, false.
 *
 * @note This function should be called once an RTCPeerConnection has been created and configured appropriately.
 */
bool
obtainInitialDescription(
    std::shared_ptr<rtc::PeerConnection> pc,
    LocalDescription &desc,
    int time_out = 10
);

