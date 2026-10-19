#pragma once
#include "rtc/rtc.hpp"

#include "Queue.hpp"

#include <memory>
#include <string>
#include <iostream>
#include <condition_variable>
#include <mutex>


typedef struct
LocalDescription{
    std::string UUID;
    std::vector<std::string> descriptions;
    std::vector<std::string> candidates;

    /** Creates a description with a fresh UUID and empty SDP/candidate lists. */
    LocalDescription();

    /**
     * @brief Copies a local description.
     * @param other Description whose UUID, SDP, and candidates are copied.
     */
    LocalDescription(const LocalDescription& other);

    /**
     * @brief Parses a serialized local description.
     * @param jsonStr JSON containing a UUID and optional Base64 description arrays.
     */
    LocalDescription(const std::string& jsonStr);

    /** @return JSON representation containing the UUID and encoded arrays. */
    std::string
    toJson() const;

    /** @return JSON fragment containing the Base64-encoded descriptions array. */
    std::string
    GetDescriptionsJson() const;

    /** @return JSON fragment containing the Base64-encoded candidates array. */
    std::string
    GetCandidatesJson() const;

    /**
     * @brief Writes this description as its JSON representation.
     * @param os Output stream receiving the JSON.
     * @param obj Description to serialize.
     * @return The output stream.
     */
    friend std::ostream& operator<<(std::ostream& os, const LocalDescription& obj);

} LocalDescription;

using LocalDescriptionPtr = std::shared_ptr<LocalDescription>;

struct FileTransferDecision {
    std::mutex mutex;
    std::condition_variable changed;
    bool fileAccepted{false};
    bool fileRejected{false};
    bool closed{false};
};

struct DataChannel {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> channel;
    QueuePtr<std::string> fileQ;
    std::shared_ptr<FileTransferDecision> fileDecision{std::make_shared<FileTransferDecision>()};
    LocalDescription description;

    /** @return true when the peer connection, data channel, and file queue exist. */
    explicit operator bool() const {
        return pc && channel && fileQ;
    }
};


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
 * Encoded line breaks are removed from the output.
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
 * @brief Creates a data channel and gathers the initial local SDP and ICE candidates.
 *
 * Creates a data channel on the supplied peer connection, waits for ICE
 * gathering to finish, and stores the resulting channel, file queue, and local
 * description in the supplied objects.
 *
 * @param dataChannel Peer connection to configure; receives the channel and queue.
 * @param desc Receives the gathered local SDP descriptions and ICE candidates.
 * @param time_out Maximum seconds to wait for ICE gathering; defaults to 10.
 *
 * @return true if a channel and local description were created before timeout; otherwise false.
 *
 * @note The peer connection must be created and configured before this call.
 */
bool
obtainInitialDescription(
    DataChannel& dataChannel,
    LocalDescription &desc,
    int time_out = 10
);

