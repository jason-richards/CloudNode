#include "rtc/rtc.hpp"
#include <rapidjson/document.h>
#include <rapidjson/error/error.h>
#include <rapidjson/stream.h>
#include <rapidjson/fwd.h>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <signal.h>
#include "parse_cl.h"

#include "RandomName.hpp"
#include "LocalDescription.hpp"
#include "ControlPort.hpp"
#include "Logger.hpp"
#include "StopToken.hpp"

#include <iostream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <string>
#include <functional>



std::map<std::string, ControlPort::Ws*>         clientDB;
std::map<std::string, std::string>              pendingOffers;
std::map<std::string, std::vector<std::string>> rooms;

StopTokenPtr g_stopToken;



/**
 * @brief Handles signals, specifically SIGINT.
 *
 * This function is called when a signal (e.g., SIGINT) is received. It requests the stop
 * of the application by setting the stop token.
 *
 * @param signalNum The number of the signal that was caught.
 * @param info Additional information about the signal.
 * @param context Contextual information about the signal.
 */
void
signalHandler(
    int signalNum,
    siginfo_t * info,
    void * context
) {
    (void) info;
    (void) context;
    switch (signalNum) {
    case SIGINT:
        RequestStop(g_stopToken);
    default:
        return;
    };
}



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
) {
    if (!pc) return false;

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    pc->onGatheringStateChange([&](rtc::PeerConnection::GatheringState state) {
        std::lock_guard<std::mutex> lock(mtx);

        if (state == rtc::PeerConnection::GatheringState::Complete ) {
            done = true;
            cv.notify_one();
        }
    });

    pc->onLocalDescription([&](rtc::Description description) {
        desc.descriptions.emplace_back(description);
	});

	pc->onLocalCandidate([&](rtc::Candidate candidate) {
        desc.candidates.emplace_back(candidate);
	});

    if (!pc->createDataChannel(std::string("Initial Description"))) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mtx);
    return cv.wait_for(
        lock,
        std::chrono::seconds(time_out),
        [&done]{
            return done;
        }
    );
}



/**
 * @brief Generates a hash value for a string using a simple hash function.
 *
 * This function is used to quickly compare command strings without using
 * potentially expensive string comparison functions multiple times.
 *
 * @param str The input string to be hashed.
 * @return An unsigned integer representing the hash of the input string.
 */
constexpr unsigned int
string_hash(
    std::string_view str
) {
    unsigned int hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<unsigned int>(c);
        hash *= 16777619u;
    }
    return hash;
}






/**
 * @brief Handles the "ping" command.
 *
 * This function processes a "ping" command from a client to check the connection
 * status by sending a "pong" response back to the client.
 *
 * @param ws Pointer to the WebSocket object representing the connection.
 * @param doc The JSON document containing the command and its parameters.
 */
void
PingCommand(
    ControlPort::Ws *    ws,
    rapidjson::Document& doc
) {
    auto user = ws->getUserData()->x_client_id;

    Logger::info() << "\'" << user << "\' sent \'ping\'; sending \'pong\'.";

    auto pong = "{\"type\" : \"pong\", \"id\" : \"" + user + "\"}";
    ws->send(pong);
}



/**
 * @brief Sends a join notification to all clients in the specified room.
 *
 * This function constructs a "join" notification JSON message containing the identifier
 * of the new client who has joined the room. The notification is then broadcasted to
 * all connected clients within the same room, informing them about the arrival of the new client.
 *
 * @param roomId Identifier of the room into which the client is joining.
 * @param clientId Unique identifier of the client who is joining.
 */
void
SendJoinNotification(
    const std::string& whoToNotify,
    const std::string& whoJoined
) {
    auto ws = clientDB.find(whoToNotify);
    if (ws == clientDB.end()) {
        Logger::error() << "User: " + whoToNotify + " not found.";
        return;
    }

    Logger::info() << "Notifying \'" << whoToNotify << "\' that \'" << whoJoined << "\' joined.";

    auto message = "{\"type\" : \"peer_joined\", \"peerId\" : \"" + whoJoined + "\"}";
    ws->second->send(message);
}



/**
 * @brief Handles the "join" command.
 *
 * This function processes a "join" command from a client, which indicates that
 * the client wishes to join a particular session or group.
 *
 * @param ws Pointer to the WebSocket object representing the connection.
 * @param doc The JSON document containing the command and its parameters.
 */
void
JoinCommand(
    ControlPort::Ws *    ws,
    rapidjson::Document& doc
) {
    auto user = ws->getUserData()->x_client_id;

    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "Join command with no room specification.";
        return;
    }

    std::string room = doc["room"].GetString();
    Logger::info()  
        << "User \'" 
        << user 
        << "\' requested to join room \'" 
        << room 
        << "\'."; 

    if (rooms.find(room) == rooms.end()) {
        rooms[room] = std::vector<std::string>();
    }

    if (std::find(rooms[room].begin(), rooms[room].end(), user) != rooms[room].end()) {
        Logger::warn() << user << " already joined " << room << ".";
        return;
    }

    clientDB[user] = ws;

    // Notify each user in room of new member
    for (auto u : rooms[room]) {
        SendJoinNotification(u, user);
    }

    rooms[room].push_back(user);
}



/**
 * @brief Sends a leave notification to all clients in the specified room.
 *
 * This function constructs a "leave" notification JSON message containing the identifier
 * of the client who is leaving the room. The notification is then broadcasted to all
 * connected clients within the same room, informing them about the departure of the client.
 *
 * @param roomId Identifier of the room from which the client is leaving.
 * @param clientId Unique identifier of the client who is leaving.
 */
void
SendLeaveNotification(
    const std::string& whoToNotify,
    const std::string& whoLeft
) {
    auto ws = clientDB.find(whoToNotify);
    if (ws == clientDB.end()) {
        Logger::warn() << "User: " + whoToNotify + " not found.";
        return;
    }

    Logger::info() << "Notifying \'" << whoToNotify << "\' that \'" << whoLeft << "\' left.";

    auto message = "{\"type\" : \"peer_left\", \"peerId\" : \"" + whoLeft + "\"}";
    ws->second->send(message);
}



void
LeaveCommand(
    ControlPort::Ws * ws,
    const std::string& user,
    const std::string& room
) {
    if (rooms.find(room) == rooms.end()) {
        Logger::warn() << "Room \'" << room << "\', does not exist.";
        return;
    }

    auto it = std::find(rooms[room].begin(), rooms[room].end(), user);
    if (it == rooms[room].end()) {
        Logger::warn() << "User \'" << user << "\' sent leave, however not part of room \'" << room << "\'.";
        return;
    }

    rooms[room].erase(it);

    // Notify each user in room of new member
    for (auto u : rooms[room]) {
        SendLeaveNotification(u, user);
    }
}



/**
 * @brief Handles the "leave" command.
 *
 * This function processes a "leave" command from a client, which indicates that
 * the client wishes to leave a particular session or group.
 *
 * @param ws Pointer to the WebSocket object representing the connection.
 * @param doc The JSON document containing the command and its parameters.
 */
void
LeaveCommand(
    ControlPort::Ws *    ws,
    rapidjson::Document& doc
) {
    auto user = ws->getUserData()->x_client_id;

    if (!doc.HasMember("room") || !doc["room"].IsString()) {
        Logger::warn() << "leave command with no room specification.";
        return;
    }

    std::string room = doc["room"].GetString();

    LeaveCommand(ws, user, room);
}




struct FileDetails {
    std::string name;
    size_t size;
    std::string mimeType;
};

std::ostream& 
operator<<(
    std::ostream& os,
    const FileDetails& file
) {
    os << "FileDetails(" 
       << "name: \"" << file.name << "\", "
       << "size: " << file.size << ", "
       << "mimeType: \"" << file.mimeType << "\")";
    return os;
}



/**
 * @brief Handles processing file commands received from a WebSocket client.
 *
 * This helper function performs tasks that are shared between the FileOfferCommand and 
 * FileAcceptCommand functions, such as validating the document structure, performing security
 * checks, modifying the document to include the sender's ID, and sending it to the intended 
 * recipient via WebSocket. If any errors occur during these steps, appropriate error messages 
 * are logged.
 *
 * @param ws A pointer to the WebSocket object from which the command was received.
 *           This object provides access to the client's user data and communication methods.
 *
 * @param doc The RapidJSON Document containing the file command details. Expected fields include:
 *            - "room" (string): The room ID where the command is being processed.
 *            - "to" (string): The recipient's client ID.
 *            - "file" (object): An object containing file details, with expected subfields:
 *              - "name" (string): The name of the file involved in the command.
 *
 * @param file A reference to a FileDetails struct that holds file information extracted from the document.
 *
 * @return void
 */
void
HandleFileCommand(
    ControlPort::Ws * ws,
    rapidjson::Document& doc,
    FileDetails& file
) {
    if (!doc.HasMember("room") || !doc["room"].IsString() ||
        !doc.HasMember("to") || !doc["to"].IsString() ||
        !doc.HasMember("file") || !doc["file"].IsObject()) {
        Logger::error() << "Invalid file command format";
        return;
    }

    std::string fromId = ws->getUserData()->x_client_id;
    std::string toId = doc["to"].GetString();
    std::string room = doc["room"].GetString();

    // Security Checks
    if (std::find(rooms[room].begin(), rooms[room].end(), fromId) == rooms[room].end()) {
        Logger::error()
            << "\'"
            << fromId
            << "\' requested to send file to \'"
            << toId
            << "\' in room \'"
            << room
            <<"\', but has not joined room.";
        return;
    }

    if (std::find(rooms[room].begin(), rooms[room].end(), toId) == rooms[room].end()) {
        Logger::error()
            << "User \'"
            << toId
            << "\' not in room \'"
            << room
            << "\'.";
        return;
    }

    doc.EraseMember("to");

    rapidjson::Value key("from", doc.GetAllocator());
    rapidjson::Value val(fromId.c_str(), doc.GetAllocator());
    doc.AddMember(key, val, doc.GetAllocator());

    auto toWs = clientDB.find(toId);
    if (toWs == clientDB.end()) {
        Logger::error()
            << "Unable to find socket for \'" 
            << toId 
            << "\'.";
        return;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    if (!toWs->second->send(buffer.GetString())) {
        Logger::error()
            << "Error encountered making offer to \'"
            << toId
            << "\'.";
        return;
    }
}



/**
 * @brief Handles the file offer command received from a WebSocket client.
 *
 * This function processes a JSON document containing details about a file offer,
 * including the file name, size, and MIME type. It performs necessary checks,
 * modifies the document to include the sender's ID, and forwards it to the intended
 * recipient via WebSocket.
 *
 * @param ws A pointer to the WebSocket object from which the command was received.
 *           This object provides access to the client's user data and communication methods.
 *
 * @param doc The RapidJSON Document containing the file offer details. Expected fields include:
 *            - "room" (string): The room ID where the offer is being sent.
 *            - "to" (string): The recipient's client ID.
 *            - "file" (object): An object containing file details, with expected subfields:
 *              - "name" (string): The name of the file being offered.
 *              - "size" (int64_t): The size of the file in bytes.
 *              - "mimeType" (string): The MIME type of the file.
 *
 * @return void
 */
void 
FileOfferCommand(
    ControlPort::Ws * ws,
    rapidjson::Document& doc
) {
    if (!doc.HasMember("file") || !doc["file"].IsObject() ||
        !doc["file"].HasMember("name") || !doc["file"]["name"].IsString() ||
        !doc["file"].HasMember("size") || !doc["file"]["size"].IsInt64() ||
        !doc["file"].HasMember("mimeType") || !doc["file"]["mimeType"].IsString()) {
        Logger::error() << "Invalid file details in file offer command";
        return;
    }

    FileDetails file;
    file.name = doc["file"]["name"].GetString();
    file.size = static_cast<size_t>(doc["file"]["size"].GetInt64());
    file.mimeType = doc["file"]["mimeType"].GetString();

    Logger::info()
        << "File offer from \'"
        << ws->getUserData()->x_client_id
        << "\', to \'" 
        << doc["to"].GetString()
        << "\': "
        << file.name;

    HandleFileCommand(ws, doc, file);
}



/**
 * @brief Handles the file accept command received from a WebSocket client.
 *
 * This function processes a JSON document containing details about an accepted file offer,
 * including the file name. It performs necessary checks, modifies the document to include
 * the sender's ID, and forwards it to the intended recipient via WebSocket.
 *
 * @param ws A pointer to the WebSocket object from which the command was received.
 *           This object provides access to the client's user data and communication methods.
 *
 * @param doc The RapidJSON Document containing the file accept details. Expected fields include:
 *            - "room" (string): The room ID where the offer is being accepted.
 *            - "to" (string): The recipient's client ID.
 *            - "file" (object): An object containing file details, with expected subfields:
 *              - "name" (string): The name of the file that was accepted.
 *
 * @return void
 */
void
FileAcceptCommand(
    ControlPort::Ws * ws,
    rapidjson::Document& doc
) {
    if (!doc.HasMember("file") || !doc["file"].IsObject() ||
        !doc["file"].HasMember("name") || !doc["file"]["name"].IsString()) {
        Logger::error() << "Invalid file details in file accept command";
        return;
    }

    FileDetails file;
    file.name = doc["file"]["name"].GetString();

    Logger::info()
        << "File offer accept from \'"
        << ws->getUserData()->x_client_id
        << "\', to \'" 
        << doc["to"].GetString()
        << "\': "
        << file.name;

    HandleFileCommand(ws, doc, file);
}



/**
 * @brief Handles the opening of a WebSocket connection.
 *
 * This function logs information when a new WebSocket connection is opened.
 *
 * @param ws Pointer to the WebSocket object that represents the connection.
 */
void
ControlOpen(
    ControlPort::Ws * ws,
    const std::string& user
) {
    Logger::info() << "\'" << user << "\' connected.";
}



/**
 * @brief Handles the closure of a WebSocket connection.
 *
 * This function logs information when an existing WebSocket connection is closed.
 *
 * @param ws Pointer to the WebSocket object that represents the connection.
 */
void
ControlClose(
    ControlPort::Ws * ws,
    int code,
    std::string_view message
) {
    auto user = ws->getUserData()->x_client_id;

    Logger::info() << "\'" << user << "\' has closed connection.";

    for (const auto& [key, value] : rooms) {
        if (std::find(rooms[key].begin(), rooms[key].end(), user) == rooms[key].end())
            continue;

        LeaveCommand(ws, user, key);
    }
}



/**
 * @brief Routes incoming WebSocket messages to appropriate handlers.
 *
 * This function parses the incoming JSON message and routes it to the
 * appropriate handler based on the command specified in the message.
 *
 * @param ws Pointer to the WebSocket object that represents the connection.
 * @param jsonStr The received JSON string message.
 */
void
ControlCommandRouter(
    ControlPort::Ws * ws,
    const std::string& jsonStr
) {
    rapidjson::Document doc;
    doc.Parse(jsonStr.data(), jsonStr.size());

    if (doc.HasParseError()) {
        return; 
    }

    if (doc.HasMember("type") && doc["type"].IsString()) {
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
}



/**
 * @brief Main entry point of the application.
 *
 * This function initializes signal handling, parses command-line arguments, configures
 * the WebRTC peer connection with a STUN server, generates a random name or uses
 * one provided via the command line, and starts a WebSocket control port to handle
 * incoming commands for peer-to-peer communication. The main thread runs in an
 * infinite loop until a stop signal is received.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return Returns 0 on successful execution, non-zero otherwise.
 */
int
main (
    int    argc,
    char * argv[]
) {
    struct sigaction newHandler, oldHandler;

    memset(&newHandler, 0, sizeof newHandler);
    memset(&oldHandler, 0, sizeof oldHandler);
    newHandler.sa_sigaction = signalHandler;
    newHandler.sa_flags = SA_SIGINFO;

    sigaction(SIGINT, &newHandler, &oldHandler);

    g_stopToken = CreateStopToken();
    Start(g_stopToken);

    rtc::Configuration config;

    Cmdline params(argc, argv);

    std::string stunServer = "";
    if (params.stunAddress().substr(0, 5).compare("stun:") != 0) {
        stunServer = "stun:";
    }

    stunServer += params.stunAddress() + ":" + std::to_string(params.stunPort());
    config.iceServers.emplace_back(stunServer);
    
    std::string name;
    if (params.n().length()) {
        name = params.n();
    } else {
        name = RandomNameGenerator::Generate();
    }

    Logger::info() 
        << "'"
        << name 
        << "' is coming online.";

    if (params.m()) {
        Logger::info()
            << "Running in server mode.";

        ControlPort control;

        control.OnOpen(ControlOpen);
        control.OnClose(ControlClose);
        control.OnMessage(ControlCommandRouter);

        // Start asynchronously on port 8080
        if (control.Start(params.p())) {
            Logger::info() << "Control Server running on port: " << params.p();
            
            // Keep main thread alive for demonstration
            while (!StopRequested(g_stopToken)) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }

            control.Stop();
        }
    } else {
        Logger::info()
            << "STUN server is '"
            << stunServer 
            << "'.";


    }

    return 0;
}

