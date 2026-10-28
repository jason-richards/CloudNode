// swift-tools-version: 6.0
import Foundation
import PackageDescription

// CMake builds the libdatachannel libraries and headers into <build>/iOS/deps/<platform>
// and exports this variable when it runs xtool; set it yourself when running xtool by hand.
let depsDir = ProcessInfo.processInfo.environment["CLOUDNODE_IOS_DEPS"]
    ?? URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .appendingPathComponent("build/iOS/deps/iphoneos").path

let package = Package(
    name: "CloudNodeClient",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        .library(name: "CloudNodeClient", targets: ["CloudNodeClient"]),
    ],
    targets: [
        .target(
            name: "CRTC",
            linkerSettings: [
                .unsafeFlags(["-L\(depsDir)/lib"], .when(platforms: [.iOS])),
                .linkedLibrary("datachannel", .when(platforms: [.iOS])),
                .linkedLibrary("juice", .when(platforms: [.iOS])),
                .linkedLibrary("usrsctp", .when(platforms: [.iOS])),
                .linkedLibrary("mbedtls", .when(platforms: [.iOS])),
                .linkedLibrary("mbedx509", .when(platforms: [.iOS])),
                .linkedLibrary("mbedcrypto", .when(platforms: [.iOS])),
                .linkedLibrary("c++"),
            ]
        ),
        .target(
            name: "CloudNodeClient",
            dependencies: ["CRTC"],
            swiftSettings: [
                .swiftLanguageMode(.v5),
                // CRTC.h includes rtc.h from the generated deps directory.
                .unsafeFlags(["-Xcc", "-I\(depsDir)/include"]),
            ]
        ),
    ]
)
