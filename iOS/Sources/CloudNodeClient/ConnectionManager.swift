import Foundation
import PhotosUI
import SwiftUI
import UniformTypeIdentifiers

struct ConnectionConfig {
    var serverAddress: String
    var stunServer: String
    var name: String
    var room: String
    var peerName: String
    var peerRoom: String
}

struct Transfer: Identifiable {
    enum Status { case waiting, sending, done, rejected, failed }
    let id = UUID()
    var name: String
    var size: Int
    var sent = 0
    var status = Status.waiting
}

@MainActor
final class ConnectionManager: ObservableObject {
    enum State: Equatable {
        case idle
        case connectingToServer
        case joiningRooms
        case waitingForPeer
        case negotiating
        case ready
        case failed(String)
    }

    @Published private(set) var state = State.idle
    @Published private(set) var transfers: [Transfer] = []
    @Published private(set) var log: [String] = []

    private static let chunkSize = 16 * 1024
    private static let highWaterMark: UInt64 = 1 << 20

    private var config: ConnectionConfig?
    private var signaling: SignalingClient?
    private var session: PeerSession?
    private var joinedRooms = Set<String>()
    private var roomPeers = [String: Set<String>]()
    private var acceptWaiters = [String: CheckedContinuation<Bool, Never>]()

    var isActive: Bool {
        switch state {
        case .idle, .failed: return false
        default: return true
        }
    }

    func connect(_ config: ConnectionConfig) {
        disconnect()
        guard let url = Self.signalingURL(config.serverAddress) else {
            state = .failed("Invalid server address")
            return
        }
        self.config = config
        log.removeAll()
        addLog("Connecting to \(url.absoluteString) as '\(config.name)'")
        state = .connectingToServer

        let signaling = SignalingClient(url: url, clientID: config.name)
        signaling.onMessage = { [weak self] message in
            Task { @MainActor in self?.handle(message) }
        }
        signaling.onClose = { [weak self] reason in
            Task { @MainActor in self?.fail("Server connection lost: \(reason)") }
        }
        self.signaling = signaling
        signaling.start()

        state = .joiningRooms
        // The server rejects a repeated join, so a shared room is joined once.
        for room in Set([config.room, config.peerRoom]) {
            signaling.send(["type": "join", "room": room])
        }
    }

    func disconnect() {
        signaling?.onClose = nil
        signaling?.close(sending: (config.map { Set([$0.room, $0.peerRoom]) } ?? []).map { ["type": "leave", "room": $0] })
        session?.onClose = nil
        session?.close()
        signaling = nil
        session = nil
        config = nil
        joinedRooms.removeAll()
        roomPeers.removeAll()
        acceptWaiters.values.forEach { $0.resume(returning: false) }
        acceptWaiters.removeAll()
        state = .idle
    }

    // MARK: Signaling

    private func handle(_ message: [String: Any]) {
        guard let type = message["type"] as? String, let config else { return }
        addLog("<- \(type) room=\(message["room"] as? String ?? "-") peerId=\(message["peerId"] as? String ?? message["peer"] as? String ?? "-")")
        switch type {
        case "join", "peer_joined":
            guard let room = message["room"] as? String,
                  let peers = message["peers"] as? [String] else { return }
            roomPeers[room] = Set(peers)
            if message["peerId"] as? String == config.name {
                joinedRooms.insert(room)
            }
            advanceIfPossible()
        case "leave", "peer_left":
            guard let room = message["room"] as? String,
                  let peers = message["peers"] as? [String] else { return }
            roomPeers[room] = Set(peers)
            if message["peerId"] as? String == config.peerName, room == config.peerRoom, state == .ready {
                fail("Peer left the room")
            }
        case "answer":
            guard let encoded = message["sdp"] as? String, let sdp = Self.decode(encoded) else { return }
            session?.applyAnswer(sdp: sdp)
        case "candidate":
            guard let candidate = message["candidate"] as? String, let mid = message["mid"] as? String else { return }
            session?.addRemoteCandidate(candidate, mid: mid)
        case "file_accept", "file_reject":
            guard let uuid = message["uuid"] as? String,
                  let waiter = acceptWaiters.removeValue(forKey: uuid) else { return }
            waiter.resume(returning: type == "file_accept")
        default:
            break
        }
    }

    private func advanceIfPossible() {
        guard let config, state != .negotiating, state != .ready,
              joinedRooms.isSuperset(of: [config.room, config.peerRoom]) else { return }
        guard roomPeers[config.peerRoom]?.contains(config.peerName) == true else {
            state = .waitingForPeer
            return
        }
        startNegotiation(config)
    }

    private func startNegotiation(_ config: ConnectionConfig) {
        state = .negotiating
        addLog("\(config.peerName) is in \(config.peerRoom); creating data channel")
        PeerSession.logHandler = { [weak self] line in
            Task { @MainActor in self?.addLog("rtc: \(line)") }
        }

        Task {
            // Creating the connection can block on STUN resolution, so keep it off the main actor.
            let session = await Task.detached { PeerSession(stunServer: config.stunServer) }.value
            guard let session else {
                fail("Unable to create peer connection")
                return
            }
            guard state == .negotiating, self.session == nil else {
                session.close()
                return
            }
            self.session = session
            session.onOpen = { [weak self] in
                Task { @MainActor in
                    self?.addLog("Data channel open")
                    if self?.state == .negotiating { self?.state = .ready }
                }
            }
            session.onClose = { [weak self] in
                Task { @MainActor in self?.fail("Data channel closed") }
            }

            do {
                let offer = try await session.makeOffer()
                addLog("Offer ready: \(offer.candidates.count) candidates")
                guard let sdp = Self.encode(offer.sdp) else { throw PeerSessionError.offer }
                signaling?.send([
                    "type": "offer",
                    "room": config.peerRoom,
                    "peer": config.peerName,
                    "descriptions": [sdp],
                    "candidates": offer.candidates.compactMap(Self.encode),
                ])
                addLog("-> offer to \(config.peerName)")
            } catch {
                fail("Offer failed: \(error.localizedDescription)")
                return
            }

            try? await Task.sleep(nanoseconds: 30_000_000_000)
            if state == .negotiating, self.session === session {
                fail("Timed out waiting for the data channel")
            }
        }
    }

    private func addLog(_ line: String) {
        NSLog("CloudNode: %@", line)
        log.append(line)
    }

    private func fail(_ reason: String) {
        guard state != .idle else { return }
        disconnect()
        state = .failed(reason)
    }

    // MARK: Photos

    func send(_ items: [PhotosPickerItem]) async {
        for item in items {
            await send(item)
        }
    }

    private func send(_ item: PhotosPickerItem) async {
        let type = item.supportedContentTypes.first
        let ext = type?.preferredFilenameExtension ?? "bin"
        let mime = type?.preferredMIMEType ?? "application/octet-stream"
        let name = "IMG_\(UUID().uuidString.prefix(8)).\(ext)"

        guard let data = try? await item.loadTransferable(type: Data.self) else {
            transfers.append(Transfer(name: name, size: 0, status: .failed))
            return
        }
        let index = transfers.count
        transfers.append(Transfer(name: name, size: data.count))

        guard let config, let signaling, let session, state == .ready else {
            transfers[index].status = .failed
            return
        }

        let uuid = UUID().uuidString
        let accepted = await withCheckedContinuation { (continuation: CheckedContinuation<Bool, Never>) in
            acceptWaiters[uuid] = continuation
            signaling.send([
                "type": "file_offer",
                "uuid": uuid,
                "room": config.peerRoom,
                "peer": config.peerName,
                "file": ["name": name, "size": data.count, "mimeType": mime],
            ])
        }
        guard accepted else {
            transfers[index].status = .rejected
            return
        }

        transfers[index].status = .sending
        var offset = 0
        while offset < data.count {
            while session.bufferedAmount > Self.highWaterMark {
                try? await Task.sleep(nanoseconds: 20_000_000)
            }
            let end = min(offset + Self.chunkSize, data.count)
            guard session.send(binary: data.subdata(in: offset..<end)) else {
                transfers[index].status = .failed
                return
            }
            offset = end
            transfers[index].sent = offset
        }

        signaling.send(["type": "file_complete", "room": config.peerRoom, "peer": config.peerName])
        transfers[index].status = .done
    }

    // MARK: Helpers

    private static func signalingURL(_ address: String) -> URL? {
        let trimmed = address.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return nil }
        let withScheme = trimmed.contains("://") ? trimmed : "ws://\(trimmed)"
        return URL(string: withScheme)
    }

    private static func encode(_ text: String) -> String? {
        text.isEmpty ? nil : Data(text.utf8).base64EncodedString()
    }

    private static func decode(_ text: String) -> String? {
        Data(base64Encoded: text, options: .ignoreUnknownCharacters).flatMap { String(data: $0, encoding: .utf8) }
    }
}
