import CRTC
import Foundation

enum PeerSessionError: Error {
    case setup
    case offer
}

/// Offering side of a CloudNode data channel, built on libdatachannel's C API like WebRTCClient::MakeDataChannel.
final class PeerSession {
    struct Offer {
        let sdp: String
        let candidates: [String]
    }

    var onOpen: (() -> Void)?
    var onClose: (() -> Void)?

    private static let channelLabel = "File Transfer"
    private static let maxMessageSize: Int32 = 2 * 1024 * 1024
    static var logHandler: ((String) -> Void)?
    private static let loggerInitialized: Void = rtcInitLogger(RTC_LOG_INFO) { _, message in
        guard let message else { return }
        PeerSession.logHandler?(String(cString: message))
    }

    private var pc: Int32 = -1
    private var channel: Int32 = -1

    private let lock = NSLock()
    private var localDescription: String?
    private var localCandidates: [String] = []
    private var gatheringComplete = false
    private var pendingRemote: [(candidate: String, mid: String)] = []
    private var hasRemoteDescription = false
    private var closed = false

    init?(stunServer: String) {
        _ = Self.loggerInitialized

        var id: Int32 = -1
        stunServer.withCString { server in
            var servers: [UnsafePointer<CChar>?] = [server]
            servers.withUnsafeMutableBufferPointer { buffer in
                var config = rtcConfiguration()
                config.iceServers = buffer.baseAddress
                config.iceServersCount = 1
                config.maxMessageSize = Self.maxMessageSize
                id = rtcCreatePeerConnection(&config)
            }
        }
        guard id >= 0 else { return nil }
        pc = id

        rtcSetUserPointer(pc, Unmanaged.passUnretained(self).toOpaque())
        rtcSetLocalDescriptionCallback(pc) { _, sdp, _, ptr in
            guard let ptr, let sdp else { return }
            PeerSession.session(ptr).handleLocalDescription(String(cString: sdp))
        }
        rtcSetLocalCandidateCallback(pc) { _, candidate, _, ptr in
            guard let ptr, let candidate else { return }
            PeerSession.session(ptr).handleLocalCandidate(String(cString: candidate))
        }
        rtcSetGatheringStateChangeCallback(pc) { _, state, ptr in
            guard let ptr, state == RTC_GATHERING_COMPLETE else { return }
            PeerSession.session(ptr).handleGatheringComplete()
        }
        rtcSetStateChangeCallback(pc) { _, state, ptr in
            guard let ptr, state == RTC_FAILED || state == RTC_CLOSED else { return }
            PeerSession.session(ptr).onClose?()
        }
    }

    deinit {
        close()
    }

    /// CloudNode expects the description and candidates together, so this waits for ICE gathering to finish.
    func makeOffer(gatherTimeout: TimeInterval = 10) async throws -> Offer {
        channel = rtcCreateDataChannel(pc, Self.channelLabel)
        guard channel >= 0 else { throw PeerSessionError.setup }

        rtcSetUserPointer(channel, Unmanaged.passUnretained(self).toOpaque())
        rtcSetOpenCallback(channel) { _, ptr in
            guard let ptr else { return }
            PeerSession.session(ptr).onOpen?()
        }
        rtcSetClosedCallback(channel) { _, ptr in
            guard let ptr else { return }
            PeerSession.session(ptr).onClose?()
        }
        // Unread incoming messages would otherwise queue up.
        rtcSetMessageCallback(channel) { _, _, _, _ in }

        let deadline = Date().addingTimeInterval(gatherTimeout)
        while Date() < deadline {
            if isGatheringComplete { break }
            try await Task.sleep(nanoseconds: 50_000_000)
        }

        guard let offer = currentOffer() else { throw PeerSessionError.offer }
        return offer
    }

    private var isGatheringComplete: Bool {
        lock.lock()
        defer { lock.unlock() }
        return gatheringComplete
    }

    private func currentOffer() -> Offer? {
        lock.lock()
        defer { lock.unlock() }
        guard let sdp = localDescription else { return nil }
        return Offer(sdp: sdp, candidates: localCandidates)
    }

    func applyAnswer(sdp: String) {
        guard rtcSetRemoteDescription(pc, sdp, "answer") >= 0 else { return }
        lock.lock()
        hasRemoteDescription = true
        let queued = pendingRemote
        pendingRemote.removeAll()
        lock.unlock()
        queued.forEach { rtcAddRemoteCandidate(pc, $0.candidate, $0.mid) }
    }

    /// Candidates can arrive before the answer; they are held until the remote description is set.
    func addRemoteCandidate(_ candidate: String, mid: String) {
        lock.lock()
        if !hasRemoteDescription {
            pendingRemote.append((candidate, mid))
            lock.unlock()
            return
        }
        lock.unlock()
        rtcAddRemoteCandidate(pc, candidate, mid)
    }

    var bufferedAmount: UInt64 {
        channel < 0 ? 0 : UInt64(max(0, rtcGetBufferedAmount(channel)))
    }

    @discardableResult
    func send(binary data: Data) -> Bool {
        guard channel >= 0 else { return false }
        return data.withUnsafeBytes { raw in
            guard let base = raw.baseAddress?.assumingMemoryBound(to: CChar.self) else { return false }
            return rtcSendMessage(channel, base, Int32(raw.count)) >= 0
        }
    }

    func close() {
        lock.lock()
        let alreadyClosed = closed
        closed = true
        lock.unlock()
        guard !alreadyClosed else { return }

        // Deleting the peer connection also waits for in-flight callbacks to finish.
        if channel >= 0 { rtcDeleteDataChannel(channel) }
        if pc >= 0 { rtcDeletePeerConnection(pc) }
    }

    private static func session(_ pointer: UnsafeMutableRawPointer) -> PeerSession {
        Unmanaged<PeerSession>.fromOpaque(pointer).takeUnretainedValue()
    }

    private func handleLocalDescription(_ sdp: String) {
        lock.lock()
        if localDescription == nil { localDescription = sdp }
        lock.unlock()
    }

    private func handleLocalCandidate(_ candidate: String) {
        lock.lock()
        localCandidates.append(candidate)
        lock.unlock()
    }

    private func handleGatheringComplete() {
        lock.lock()
        gatheringComplete = true
        lock.unlock()
    }
}
