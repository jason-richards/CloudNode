#!/usr/bin/env python3
"""Server, a receiving client, and a sender that calls WebRTCClient::QueueFile.

The sender (QueueFileClient) waits 10 seconds after startup, then queues a file for
the receiver. The receiver must get the file intact.
"""

import os
import pathlib
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

ROOM = "queue-api-room"


def reserve_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_for_port(port, timeout, processes):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(p.poll() is not None for p in processes):
            raise RuntimeError("A process exited before the server became ready")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError("CloudNode signalling server did not start listening")


def wait_for(predicate, description, timeout, processes):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        for p in processes:
            if p.poll() is not None:
                raise RuntimeError(f"Process {p.pid} exited while waiting for {description}")
        time.sleep(0.1)
    raise TimeoutError(f"Timed out waiting for {description}")


def read(log_path):
    return log_path.read_text(errors="replace")


def stop_process(process):
    if process is None or process.poll() is not None:
        return
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: queue_file_api_transfer.py CLOUDNODE_EXECUTABLE QUEUEFILECLIENT_EXECUTABLE")

    cloudnode = str(pathlib.Path(sys.argv[1]).resolve())
    queue_client = str(pathlib.Path(sys.argv[2]).resolve())
    stdbuf = shutil.which("stdbuf")
    command_prefix = [stdbuf, "-oL", "-eL"] if stdbuf else []
    processes = []
    log_files = []

    with tempfile.TemporaryDirectory(prefix="cloudnode-queueapi-") as temp_dir:
        root = pathlib.Path(temp_dir)
        send_dir = root / "sender"
        recv_dir = root / "receiver"
        send_dir.mkdir()
        recv_dir.mkdir()

        file_name = "queued-by-api.wav"
        payload = os.urandom(96 * 1024 + 7)
        (send_dir / file_name).write_bytes(payload)
        port = reserve_port()

        def launch(executable, name, arguments):
            log_path = root / f"{name}.log"
            log_file = log_path.open("w+")
            log_files.append(log_file)
            process = subprocess.Popen(
                [*command_prefix, executable, "--name", name, "--messageAddress", "ws://127.0.0.1",
                 "--messagePort", str(port), "--timeout", "60", *arguments],
                stdout=log_file,
                stderr=subprocess.STDOUT,
            )
            processes.append(process)
            return process, log_path

        server = receiver = sender = None
        try:
            server, server_log = launch(cloudnode, "signalling-server", ["--server"])
            wait_for_port(port, 10, processes)

            receiver, recv_log = launch(
                cloudnode, "receiver", ["--directory", str(recv_dir), "--room", ROOM]
            )
            wait_for(
                lambda: "has joined room" in read(server_log),
                "receiver to join the room",
                15,
                [server, receiver],
            )

            sender, send_log = launch(
                queue_client,
                "sender",
                ["--directory", str(send_dir), "--file", f"receiver:{ROOM}:{send_dir / file_name}"],
            )
            all_procs = [server, receiver, sender]
            started = time.monotonic()

            wait_for(
                lambda: "Queued file" in read(send_log),
                "sender to queue the file",
                30,
                all_procs,
            )
            if time.monotonic() - started < 9:
                raise AssertionError("Sender queued the file before the 10 second wait elapsed")
            if "SendFileOffer" in read(send_log).split("Queued file")[0]:
                raise AssertionError("Sender offered the file before QueueFile was called")

            wait_for(
                lambda: (recv_dir / file_name).exists() and (recv_dir / file_name).read_bytes() == payload,
                "receiver to get the queued file",
                60,
                all_procs,
            )
            wait_for(
                lambda: f"File transfer to 'receiver' in room '{ROOM}' completed." in read(send_log)
                or f"File transfer to 'sender' in room '{ROOM}' completed." in read(recv_log),
                "transfer completion notice",
                15,
                all_procs,
            )
            print("File queued through QueueFile was delivered.")
        except Exception:
            for log_path in sorted(root.glob("*.log")):
                print(f"\n--- {log_path.name} ---", file=sys.stderr)
                print(read(log_path), file=sys.stderr)
            raise
        finally:
            stop_process(sender)
            stop_process(receiver)
            stop_process(server)
            for log_file in log_files:
                log_file.close()


if __name__ == "__main__":
    main()
