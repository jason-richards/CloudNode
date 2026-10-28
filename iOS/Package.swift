// swift-tools-version: 6.0
import Foundation
import PackageDescription

// Static libraries are produced by scripts/build-libdatachannel.sh.
let vendorLibs = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent()
    .appendingPathComponent("Vendor/iphoneos/lib").path

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
                .unsafeFlags(["-L\(vendorLibs)"], .when(platforms: [.iOS])),
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
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
