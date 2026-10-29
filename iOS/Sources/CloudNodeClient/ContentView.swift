import PhotosUI
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @EnvironmentObject private var manager: ConnectionManager

    @AppStorage("serverAddress") private var serverAddress = "ws://localhost:8080"
    @AppStorage("stunServer") private var stunServer = "stun:stun.l.google.com:19302"
    @AppStorage("name") private var name = ""
    @AppStorage("room") private var room = ""
    @AppStorage("peerName") private var peerName = ""
    @AppStorage("peerRoom") private var peerRoom = ""

    @State private var selection: [PhotosPickerItem] = []
    @State private var isFileImporterPresented = false

    private var canConnect: Bool {
        ![serverAddress, name, room, peerName, peerRoom].contains { $0.trimmingCharacters(in: .whitespaces).isEmpty }
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Server") {
                    TextField("ws://host:port", text: $serverAddress)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .keyboardType(.URL)
                    TextField("STUN server", text: $stunServer)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                }
                Section("This device") {
                    TextField("Name", text: $name)
                    TextField("Room", text: $room)
                }
                Section("Remote client") {
                    TextField("Name", text: $peerName)
                    TextField("Room", text: $peerRoom)
                }
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()

                Section {
                    Button(manager.isActive ? "Disconnect" : "Connect") {
                        if manager.isActive {
                            manager.disconnect()
                        } else {
                            manager.connect(currentConfig)
                        }
                    }
                    .disabled(!manager.isActive && !canConnect)
                    Text(statusText)
                        .foregroundStyle(statusColor)
                }
                .textInputAutocapitalization(.never)

                if manager.state == .ready {
                    Section("Photos") {
                        PhotosPicker(selection: $selection, matching: .images) {
                            Label("Select Photos to Send", systemImage: "photo.on.rectangle")
                        }
                        ForEach(manager.transfers.filter { $0.category == .photo }) { transfer in
                            TransferRow(transfer: transfer)
                        }
                    }
                    Section("Videos") {
                        PhotosPicker(selection: $selection, matching: .videos) {
                            Label("Select Videos to Send", systemImage: "photo.on.rectangle")
                        }
                        ForEach(manager.transfers.filter { $0.category == .video }) { transfer in
                            TransferRow(transfer: transfer)
                        }
                    }
                    Section("Files") {
                        Button {
                            isFileImporterPresented = true
                        } label: {
                            Label("Select File to Send", systemImage: "doc")
                        }
                        ForEach(manager.transfers.filter { $0.category == .file }) { transfer in
                            TransferRow(transfer: transfer)
                        }
                    }
                }

                if !manager.log.isEmpty {
                    Section("Log") {
                        ForEach(Array(manager.log.enumerated()), id: \.offset) { _, line in
                            Text(line).font(.caption2.monospaced())
                        }
                    }
                }
            }
            .navigationTitle("CloudNode")
            .fileImporter(
                isPresented: $isFileImporterPresented,
                allowedContentTypes: [.item]
            ) { result in
                guard case .success(let url) = result else { return }
                Task { await manager.send(url) }
            }
            .onChange(of: selection) { _, items in
                guard !items.isEmpty else { return }
                selection = []
                Task { await manager.send(items) }
            }
        }
    }

    private var currentConfig: ConnectionConfig {
        ConnectionConfig(
            serverAddress: serverAddress,
            stunServer: stunServer,
            name: name.trimmingCharacters(in: .whitespaces),
            room: room.trimmingCharacters(in: .whitespaces),
            peerName: peerName.trimmingCharacters(in: .whitespaces),
            peerRoom: peerRoom.trimmingCharacters(in: .whitespaces)
        )
    }

    private var statusText: String {
        switch manager.state {
        case .idle: return "Not connected"
        case .connectingToServer: return "Connecting to server..."
        case .joiningRooms: return "Joining rooms..."
        case .waitingForPeer: return "Waiting for \(peerName) to join \(peerRoom)..."
        case .negotiating: return "Establishing data channel..."
        case .ready: return "Connected to \(peerName)"
        case .failed(let reason): return reason
        }
    }

    private var statusColor: Color {
        switch manager.state {
        case .ready: return .green
        case .failed: return .red
        default: return .secondary
        }
    }
}

private struct TransferRow: View {
    let transfer: Transfer

    var body: some View {
        VStack(alignment: .leading) {
            Text(transfer.name)
            switch transfer.status {
            case .sending:
                ProgressView(value: Double(transfer.sent), total: Double(max(transfer.size, 1)))
            case .waiting: Text("Waiting for peer to accept...").font(.caption)
            case .done: Text("Sent").font(.caption).foregroundStyle(.green)
            case .rejected: Text("Rejected by peer").font(.caption).foregroundStyle(.orange)
            case .failed: Text("Failed").font(.caption).foregroundStyle(.red)
            }
        }
    }
}
