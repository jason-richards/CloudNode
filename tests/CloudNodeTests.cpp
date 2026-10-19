#include "LocalDescription.hpp"
#include "MimeDetector.hpp"
#include "PropertyBag.hpp"
#include "Queue.hpp"
#include "RandomName.hpp"
#include "StopToken.hpp"
#include "WebRTCClient.hpp"
#include "parse_cl.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

TEST(PropertyBagTests, StoresAndRetrievesSupportedTypes) {
    PropertyBag properties;
    properties.Set("name", std::string("client-a"));
    properties.Set("timeout", 12);
    properties.Set("server", true);

    EXPECT_TRUE(properties.Contains("name"));
    EXPECT_EQ(properties.Get<std::string>("name"), "client-a");
    EXPECT_EQ(properties.Get<int>("timeout"), 12);
    EXPECT_TRUE(properties.Get<bool>("server"));
    EXPECT_FALSE(properties.Contains("missing"));
}

TEST(PropertyBagTests, RejectsMissingKeysAndWrongTypes) {
    PropertyBag properties;
    properties.Set("timeout", 12);

    EXPECT_THROW(properties.Get<int>("missing"), std::out_of_range);
    EXPECT_THROW(properties.Get<std::string>("timeout"), std::bad_variant_access);
}

TEST(QueueTests, PreservesFifoOrder) {
    auto queue = CreateStringQueue();
    AddItem(queue, std::make_shared<std::string>("first"));
    AddItem(queue, std::make_shared<std::string>("second"));

    EXPECT_EQ(GetSize(queue), 2u);
    EXPECT_EQ(*GetItem(queue), "first");
    EXPECT_EQ(*GetItem(queue), "second");
    EXPECT_EQ(GetSize(queue), 0u);
}

TEST(QueueTests, WakesAWaitingConsumerWhenAnItemArrives) {
    auto queue = CreateStringQueue();
    std::string received;
    std::thread consumer([&]() {
        received = *GetItem(queue);
    });

    AddItem(queue, std::make_shared<std::string>("ready"));
    consumer.join();

    EXPECT_EQ(received, "ready");
}

TEST(StopTokenTests, TracksStartAndStopTransitions) {
    auto token = CreateStopToken();
    EXPECT_TRUE(StopRequested(token));

    Start(token);
    EXPECT_FALSE(StopRequested(token));

    RequestStop(token);
    EXPECT_TRUE(StopRequested(token));
}

TEST(CommandLineTests, ReadsDirectoryLongOption) {
    char program[] = "CloudNode";
    char option[] = "--directory";
    char directory[] = "/tmp/cloudnode-received";
    char* argv[] = {program, option, directory};

    Cmdline parameters(3, argv);

    EXPECT_EQ(parameters.directory(), "/tmp/cloudnode-received");
}

TEST(CommandLineTests, ReadsDirectoryShortOption) {
    char program[] = "CloudNode";
    char option[] = "-d";
    char directory[] = "/tmp/cloudnode-short";
    char* argv[] = {program, option, directory};

    Cmdline parameters(3, argv);

    EXPECT_EQ(parameters.directory(), "/tmp/cloudnode-short");
}

TEST(CommandLineTests, UsesDocumentedDefaults) {
    char program[] = "CloudNode";
    char* argv[] = {program};

    Cmdline parameters(1, argv);

    EXPECT_EQ(parameters.timeout(), 10);
    EXPECT_EQ(parameters.messagePort(), 8080);
    EXPECT_EQ(parameters.stunPort(), 19302);
    EXPECT_EQ(parameters.room(), "DefaultRoom#1");
    EXPECT_FALSE(parameters.server());
}

TEST(WebRTCClientTests, ReplacesRoomPeersFromPeerJoinedRoster) {
    PropertyBag properties;
    properties.Set("name", std::string("client-a"));
    WebRTCClient client(properties);

    client.MessageRouter(
        R"({"type":"peer_joined","peerId":"client-b","room":"room-a","peers":["client-a","client-b"]})"
    );
    std::vector<std::string> peers;
    ASSERT_TRUE(client.GetPeers("room-a", peers));
    EXPECT_EQ(peers, (std::vector<std::string>{"client-a", "client-b"}));

    client.MessageRouter(
        R"({"type":"peer_joined","peerId":"client-c","room":"room-a","peers":["client-a","client-c"]})"
    );
    ASSERT_TRUE(client.GetPeers("room-a", peers));
    EXPECT_EQ(peers, (std::vector<std::string>{"client-a", "client-c"}));
}

TEST(WebRTCClientTests, IgnoresMalformedRoomPeerRoster) {
    PropertyBag properties;
    properties.Set("name", std::string("client-a"));
    WebRTCClient client(properties);
    client.MessageRouter(
        R"({"type":"peer_joined","peerId":"client-b","room":"room-a","peers":["client-a","client-b"]})"
    );

    client.MessageRouter(
        R"({"type":"peer_joined","peerId":"client-c","room":"room-a","peers":["client-a",7]})"
    );
    std::vector<std::string> peers;
    ASSERT_TRUE(client.GetPeers("room-a", peers));
    EXPECT_EQ(peers, (std::vector<std::string>{"client-a", "client-b"}));
}

TEST(WebRTCTests, ReturnsFalseForUnknownRoomWithoutChangingOutput) {
    PropertyBag properties;
    properties.Set("name", std::string("client-a"));
    WebRTCClient client(properties);
    std::vector<std::string> peers{"existing"};

    EXPECT_FALSE(client.GetPeers("missing", peers));
    EXPECT_EQ(peers, (std::vector<std::string>{"existing"}));
}

TEST(LocalDescriptionTests, EncodesAndDecodesNonEmptyPayloads) {
    const std::string original("CloudNode\0payload", 17);
    std::string encoded;
    std::string decoded;

    ASSERT_TRUE(base64Encode(original, encoded));
    EXPECT_TRUE(encoded.find('\n') == std::string::npos);
    ASSERT_TRUE(base64Decode(encoded, decoded));
    EXPECT_EQ(decoded, original);
    EXPECT_FALSE(base64Encode("", encoded));
    EXPECT_FALSE(base64Decode("", decoded));
}

TEST(LocalDescriptionTests, GeneratesUuidWithVersionFourShape) {
    const std::string uuid = generateUUID();
    const std::regex uuidV4("^[0-9A-F]{8}-[0-9A-F]{4}-4[0-9A-F]{3}-[89AB][0-9A-F]{3}-[0-9A-F]{12}$");

    EXPECT_TRUE(std::regex_match(uuid, uuidV4));
}

TEST(LocalDescriptionTests, SerializesAndRestoresDescriptionsAndCandidates) {
    LocalDescription original;
    original.descriptions.emplace_back("v=0\r\n");
    original.candidates.emplace_back("candidate:1 1 UDP");

    LocalDescription restored(original.toJson());

    EXPECT_EQ(restored.UUID, original.UUID);
    ASSERT_EQ(restored.descriptions.size(), 1u);
    ASSERT_EQ(restored.candidates.size(), 1u);
    EXPECT_EQ(restored.descriptions.front(), original.descriptions.front());
    EXPECT_EQ(restored.candidates.front(), original.candidates.front());
}

TEST(MimeDetectorTests, ReportsFileNameSizeAndMimeType) {
    const auto tempPath = std::filesystem::temp_directory_path() / "cloudnode-mime-test.txt";
    {
        std::ofstream file(tempPath, std::ios::binary);
        ASSERT_TRUE(file.good());
        file << "plain text fixture\n";
    }

    MimeDetector detector;
    std::string metadata;
    const bool generated = detector.get_file_info(tempPath.string(), metadata);
    std::filesystem::remove(tempPath);

    ASSERT_TRUE(generated) << metadata;
    EXPECT_NE(metadata.find(tempPath.string()), std::string::npos);
    EXPECT_NE(metadata.find("\"size\": 19"), std::string::npos);
    EXPECT_NE(metadata.find("text/plain"), std::string::npos);
}

TEST(RandomNameTests, GeneratesNamesWithExpectedStructure) {
    const std::string name = RandomNameGenerator::Generate();
    const std::regex generatedName("^[A-Za-z]+#[0-9]{3}$");

    EXPECT_TRUE(std::regex_match(name, generatedName));
}

TEST(FileReceiveTests, DISABLED_IncomingChunksAreWrittenUnderConfiguredDirectory) {
    GTEST_FAIL() << "TODO: deliver an incoming file offer and binary chunks, then assert "
                    "that the reconstructed file is written beneath the --directory path "
                    "with the offered filename and exact chunk contents.";
}

} // namespace