#!/usr/bin/env python3
"""Server plus two clients exchanging files.

Client 1 offers a file to client 2 before client 2 is online. The offer must be
queued and delivered as soon as client 2 joins. Client 2 then sends a file back.
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

ROOM = "queued-room"


def reserve_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_for_port(port, timeout, processes):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(p.poll() is not None for p in processes):
            raise RuntimeError("A CloudNode process exited before the server became ready")
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
                raise RuntimeError(f"CloudNode process {p.pid} exited while waiting for {description}")
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
    if len(sys.argv) != 2:
        raise SystemExit("usage: queued_file_transfer.py CLOUDNODE_EXECUTABLE")

    executable = str(pathlib.Path(sys.argv[1]).resolve())
    stdbuf = shutil.which("stdbuf")
    command_prefix = [stdbuf, "-oL", "-eL"] if stdbuf else []
    processes = []
    log_files = []

    with tempfile.TemporaryDirectory(prefix="cloudnode-queued-") as temp_dir:
        root = pathlib.Path(temp_dir)
        dir1 = root / "client1"
        dir2 = root / "client2"
        dir1.mkdir()
        dir2.mkdir()

        name1_file = "from-client-1.wav"
        name2_file = "from-client-2.wav"
        payload1 = os.urandom(96 * 1024 + 11)
        payload2 = os.urandom(64 * 1024 + 29)
        (dir1 / name1_file).write_bytes(payload1)
        (dir2 / name2_file).write_bytes(payload2)
        port = reserve_port()

        def launch(name, arguments):
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

        server = client1 = client2 = None
        try:
            server, server_log = launch("signalling-server", ["--server"])
            wait_for_port(port, 10, processes)

            # Client 1 offers a file to a peer that is not online yet.
            client1, log1 = launch(
                "client-1",
                ["--room", ROOM, "--peer", "client-2", "--directory", str(dir1),
                 "--file", str(dir1 / name1_file)],
            )
            wait_for(
                lambda: "has joined room" in read(server_log),
                "client-1 to join the room",
                15,
                [server, client1],
            )

            # Hold client 2 back; client 1 must defer its offer until then.
            time.sleep(2)
            if "SendFileOffer" in read(log1):
                raise AssertionError("Client 1 made its offer before client 2 was online")
            if client1.poll() is not None:
                raise AssertionError("Client 1 exited while waiting for client 2")

            client2, log2 = launch(
                "client-2",
                ["--room", ROOM, "--peer", "client-1", "--directory", str(dir2),
                 "--file", str(dir2 / name2_file)],
            )
            all_procs = [server, client1, client2]
            join_deadline_start = time.monotonic()

            # The queued offer must reach client 2 as soon as it is online.
            wait_for(
                lambda: f"Received file offer peer 'client-1' in room '{ROOM}': {name1_file}" in read(log2),
                "queued offer from client-1 to reach client-2",
                20,
                all_procs,
            )
            print(f"Queued offer delivered {time.monotonic() - join_deadline_start:.1f}s after client-2 started.")

            # Client 1 -> client 2 transfer.
            wait_for(
                lambda: (dir2 / name1_file).exists() and (dir2 / name1_file).read_bytes() == payload1,
                "client-2 to receive client-1's file",
                60,
                all_procs,
            )
            wait_for(
                lambda: f"File transfer to 'client-1' in room '{ROOM}' completed." in read(log2),
                "client-2 completion notice",
                15,
                all_procs,
            )

            # Client 2 -> client 1 transfer.
            wait_for(
                lambda: f"Received file offer peer 'client-2' in room '{ROOM}': {name2_file}" in read(log1),
                "client-2's offer to reach client-1",
                30,
                all_procs,
            )
            wait_for(
                lambda: (dir1 / name2_file).exists() and (dir1 / name2_file).read_bytes() == payload2,
                "client-1 to receive client-2's file",
                60,
                all_procs,
            )
            wait_for(
                lambda: f"File transfer to 'client-2' in room '{ROOM}' completed." in read(log1),
                "client-1 completion notice",
                15,
                all_procs,
            )
            print("Both files transferred; queued offer was delivered when client-2 came online.")
        except Exception:
            for log_path in sorted(root.glob("*.log")):
                print(f"\n--- {log_path.name} ---", file=sys.stderr)
                print(read(log_path), file=sys.stderr)
            raise
        finally:
            stop_process(client2)
            stop_process(client1)
            stop_process(server)
            for log_file in log_files:
                log_file.close()


if __name__ == "__main__":
    main()
