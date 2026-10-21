#!/usr/bin/env python3
"""One sender with three queued files; receivers 2, 3 and 4 come online one at a time.

Each file must be offered only once its recipient has joined, and every recipient
must end up with a byte-identical copy.
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

ROOM = "staggered-room"
RECEIVERS = ["client-2", "client-3", "client-4"]
STAGGER_SECONDS = 3


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
        raise SystemExit("usage: staggered_file_transfer.py CLOUDNODE_EXECUTABLE")

    executable = str(pathlib.Path(sys.argv[1]).resolve())
    stdbuf = shutil.which("stdbuf")
    command_prefix = [stdbuf, "-oL", "-eL"] if stdbuf else []
    processes = []
    log_files = []

    with tempfile.TemporaryDirectory(prefix="cloudnode-staggered-") as temp_dir:
        root = pathlib.Path(temp_dir)
        sender_dir = root / "sender"
        sender_dir.mkdir()

        file_names = {r: f"for-{r}.wav" for r in RECEIVERS}
        payloads = {r: os.urandom(48 * 1024 + 7 * (i + 1)) for i, r in enumerate(RECEIVERS)}
        for r in RECEIVERS:
            (sender_dir / file_names[r]).write_bytes(payloads[r])
        receiver_dirs = {r: root / r for r in RECEIVERS}
        for d in receiver_dirs.values():
            d.mkdir()
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

        server = sender = None
        receivers = {}
        try:
            server, server_log = launch("signalling-server", ["--server"])
            wait_for_port(port, 10, processes)

            specs = ",".join(f"{r}:{ROOM}:{sender_dir / file_names[r]}" for r in RECEIVERS)
            sender, sender_log = launch("sender", ["--directory", str(sender_dir), "--file", specs])
            wait_for(
                lambda: "has joined room" in read(server_log),
                "sender to join the room",
                15,
                [server, sender],
            )

            for index, r in enumerate(RECEIVERS):
                time.sleep(STAGGER_SECONDS)
                log = read(sender_log)
                if f"SendFileOffer peer='{r}'" in log:
                    raise AssertionError(f"Sender offered a file to {r} before it was online")
                for later in RECEIVERS[index + 1:]:
                    if f"SendFileOffer peer='{later}'" in log:
                        raise AssertionError(f"Sender offered a file to {later} out of order")

                process, log_path = launch(
                    r, ["--room", ROOM, "--directory", str(receiver_dirs[r])]
                )
                receivers[r] = (process, log_path)
                active = [server, sender, *(p for p, _ in receivers.values())]

                target = receiver_dirs[r] / file_names[r]
                wait_for(
                    lambda: target.exists() and target.read_bytes() == payloads[r],
                    f"{r} to receive its file",
                    60,
                    active,
                )
                wait_for(
                    lambda: f"File transfer to 'sender' in room '{ROOM}' completed." in read(log_path),
                    f"{r} completion notice",
                    15,
                    active,
                )

            for r in RECEIVERS:
                for other in RECEIVERS:
                    if other != r and (receiver_dirs[r] / file_names[other]).exists():
                        raise AssertionError(f"{r} received a file meant for {other}")
            print("All three staggered receivers got exactly their own file.")
        except Exception:
            for log_path in sorted(root.glob("*.log")):
                print(f"\n--- {log_path.name} ---", file=sys.stderr)
                print(read(log_path), file=sys.stderr)
            raise
        finally:
            for process, _ in receivers.values():
                stop_process(process)
            stop_process(sender)
            stop_process(server)
            for log_file in log_files:
                log_file.close()


if __name__ == "__main__":
    main()
