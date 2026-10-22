# CloudNode

CloudNode transfers files between clients using WebRTC data channels. A CloudNode
instance can run either as a WebSocket signalling server or as a client. The
signalling server coordinates rooms and exchanges connection and transfer
messages; file contents are sent directly between clients over the WebRTC data
channel.

## Server Mode

Start one instance in server mode to provide signalling for clients:

```sh
./build/CloudNode --server --messagePort 8080
```

The server listens on the configured message port (8080 by default). It tracks
connected clients and room membership, and relays WebRTC negotiation messages,
file offers, accept/reject responses, and completion notifications. It does
not relay the file bytes.

For clients on another machine, set `--messageAddress` to the server's
WebSocket address and make the server port reachable. For example,
`--messageAddress ws://192.0.2.10 --messagePort 8080`.

## Client Mode

Without `--server`, CloudNode runs as a client. Each client needs a unique name
within the server and must join the same room as its peers. The defaults are
`ws://localhost:8080` for signalling and `DefaultRoom#1` for the room.

Start the receiving client first:

```sh
./build/CloudNode --name receiver --room project --directory ./received
```

Then start a sender and offer a file to that client:

```sh
./build/CloudNode --name sender \
  --file receiver:project:./documents/report.pdf
```

The `--file` option accepts comma-separated `peer:room:path` entries, so one
client can offer files to multiple peers or rooms:

```sh
./build/CloudNode --name sender \
  --file 'receiver:project:./report.pdf,backup:archive:./photos.zip'
```

The target peer must be connected and present in the specified room. CloudNode
joins the default/configured `--room` and every room named by a `--file` entry.
If `--name` is omitted, CloudNode generates a unique client name automatically.
The receiver writes incoming files under `--directory`, creating parent
directories as needed; if no
directory is supplied, it writes under the current working directory. The
sender's offered filename is relative to its configured `--directory` when
possible, which lets the receiver preserve subdirectories beneath its own
destination directory.

## Keeping Two Directories in Sync

Give each client a `--directory` and set `--peer` to the other client's name.
Once the peer is online, the client opens a data channel to it and watches its
directory tree. Every new or modified file, including files in subdirectories
created later, is sent to the peer; deletions are not propagated.

```sh
./build/CloudNode --name alice --room sync --peer bob   --directory ./alice-files
./build/CloudNode --name bob   --room sync --peer alice --directory ./bob-files
```

Either client may start first; each waits for the other to appear. Files
received from a peer are not sent back. If the connection to the server is
lost, the clients reconnect and re-establish the channel.

With `--peer`, `--file` is a plain comma-separated list of paths sent to that
peer in `--room` when the channel opens, instead of `peer:room:path` entries.

## How File Transfer Works

1. Clients connect to the signalling server over WebSocket and join rooms. The
	server notifies room members when peers join or leave.
2. The sender waits for the target peer in the requested room, then negotiates a
	WebRTC peer connection. SDP descriptions and ICE candidates pass through the
	signalling server; STUN assists peers in establishing a direct connection.
3. The sender sends a file offer containing the filename, size, MIME type, and
	CRC. The receiver prepares a destination file and accepts the offer. If a
	file already exists there with a matching CRC, the receiver rejects the
	duplicate instead.
4. After acceptance, the sender streams the file as binary chunks over the
	WebRTC data channel. The default maximum message size is 2 MiB, and sending
	observes data-channel buffer capacity. The receiver writes chunks to disk
	until the offered size is reached.
5. The sender notifies the signalling server when sending completes, and the
	server forwards that notification to the receiver.

The `--file` value uses commas between entries and the first two colons in each
entry separate peer, room, and path. Avoid commas in file paths because commas
are entry separators.

## Options

| Option | Default | Description |
| --- | --- | --- |
| `-s`, `--server` | Client mode | Run the signalling server. |
| `-n`, `--name NAME` | Automatically generated unique name | Client identity. |
| `-r`, `--room ROOM` | `DefaultRoom#1` | Room to join. |
| `-p`, `--peer NAME` | None | Peer to connect to at startup; changed files in `--directory` are sent to it. |
| `-f`, `--file LIST` | None | Files to offer: `peer:room:path` entries, or plain paths when `--peer` is set. |
| `-d`, `--directory DIR` | Current directory for received files | Receive destination; also used to make offered file names relative when possible. |
| `--messageAddress URL` | `ws://localhost` | Signalling server address. |
| `--messagePort PORT` | `8080` | Signalling server port. |
| `--stunAddress HOST` | `stun.l.google.com` | STUN server host for WebRTC connectivity. |
| `--stunPort PORT` | `19302` | STUN server port. |
| `-t`, `--timeout SECONDS` | `10` | Signalling reconnect and client wait timeout. |
| `-m`, `--maxMessageSize BYTES` | `2097152` | WebRTC data-channel message buffer/chunk size. |
| `-h`, `--help` | | Show command-line help. |
| `-v`, `--version` | | Show version information. |

## Build and Tests

Configure, build, and run the test suite with:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests cover core utilities, command-line options, room membership, metadata,
and end-to-end file transfer scenarios using a local signalling server and
clients.
