# CloudNode
=======

## Tests

Configure, build, and run the test suite with:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests cover core utilities, command-line options, WebRTC room roster handling, and file metadata generation. `CloudNodeThreeProcessFileTransfer` launches a signalling server and two clients, transfers random bytes in a `.wav` file from client B to client A, and verifies the received byte count and completion notification. It uses loopback networking and a dynamically assigned port.

The disabled `FileReceiveTests.IncomingChunksAreWrittenUnderConfiguredDirectory` test records a known gap: incoming binary data-channel chunks are logged but not written to disk. When implemented, received files must be reconstructed beneath the directory selected with `--directory` / `-d`, using the offered filename, and the test should be enabled.
