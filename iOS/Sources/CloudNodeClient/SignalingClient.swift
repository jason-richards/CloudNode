import Foundation

/// WebSocket link to the CloudNode server; the server identifies us by the `x-client-id` header.
final class SignalingClient {
    var onMessage: (([String: Any]) -> Void)?
    var onClose: ((String) -> Void)?

    private let task: URLSessionWebSocketTask
    private var closed = false

    init(url: URL, clientID: String) {
        var request = URLRequest(url: url)
        request.setValue(clientID, forHTTPHeaderField: "x-client-id")
        task = URLSession.shared.webSocketTask(with: request)
    }

    func start() {
        task.resume()
        receive()
    }

    func send(_ object: [String: Any]) {
        guard let data = try? JSONSerialization.data(withJSONObject: object),
              let text = String(data: data, encoding: .utf8) else { return }
        task.send(.string(text)) { [weak self] error in
            if let error { self?.finish(error.localizedDescription) }
        }
    }

    func stop() {
        closed = true
        task.cancel(with: .normalClosure, reason: nil)
    }

    /// Cancelling right after send() drops queued frames, so close only once every message is flushed.
    func close(sending messages: [[String: Any]]) {
        closed = true
        let group = DispatchGroup()
        for message in messages {
            guard let data = try? JSONSerialization.data(withJSONObject: message),
                  let text = String(data: data, encoding: .utf8) else { continue }
            group.enter()
            task.send(.string(text)) { _ in group.leave() }
        }
        group.notify(queue: .main) { [task] in
            task.cancel(with: .normalClosure, reason: nil)
        }
    }

    private func receive() {
        task.receive { [weak self] result in
            guard let self else { return }
            switch result {
            case .failure(let error):
                self.finish(error.localizedDescription)
            case .success(let message):
                // CloudNode's uWebSockets server sends JSON in binary frames.
                let data: Data?
                switch message {
                case .string(let text): data = Data(text.utf8)
                case .data(let bytes): data = bytes
                @unknown default: data = nil
                }
                if let data,
                   let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                    self.onMessage?(json)
                }
                self.receive()
            }
        }
    }

    private func finish(_ reason: String) {
        guard !closed else { return }
        closed = true
        onClose?(reason)
    }
}
