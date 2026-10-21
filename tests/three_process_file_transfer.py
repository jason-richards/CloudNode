#!/usr/bin/env python3

import os
import pathlib
import re
import signal
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def reserve_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_for_port(port, timeout, processes):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(process.poll() is not None for process in processes):
            raise RuntimeError("A CloudNode process exited before the server became ready")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError("CloudNode signalling server did not start listening")


def wait_for_log(log_path, predicate, timeout, processes):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        content = log_path.read_text(errors="replace")
        if predicate(content):
            return content
        for process in processes:
            if process.poll() is not None:
                raise RuntimeError(f"CloudNode process {process.pid} exited unexpectedly")
        time.sleep(0.1)
    raise TimeoutError(f"Timed out waiting for expected event in {log_path.name}")


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
        raise SystemExit("usage: three_process_file_transfer.py CLOUDNODE_EXECUTABLE")

    executable = str(pathlib.Path(sys.argv[1]).resolve())
    stdbuf = shutil.which("stdbuf")
    command_prefix = [stdbuf, "-oL", "-eL"] if stdbuf else []
    processes = []
    log_files = []
    with tempfile.TemporaryDirectory(prefix="cloudnode-three-peer-") as temp_dir:
        root = pathlib.Path(temp_dir)
        receive_dir = root / "received"
        receive_dir.mkdir()
        source_file = root / "random-payload.wav"
        payload = os.urandom(192 * 1024 + 37)
        source_file.write_bytes(payload)
        port = reserve_port()

        def launch(name, arguments):
            log_path = root / f"{name}.log"
            log_file = log_path.open("w+")
            log_files.append(log_file)
            process = subprocess.Popen(
                [*command_prefix, executable, "--name", name, "--messageAddress", "ws://127.0.0.1",
                 "--messagePort", str(port), "--timeout", "30", *arguments],
                stdout=log_file,
                stderr=subprocess.STDOUT,
            )
            processes.append(process)
            return process, log_path

        server = None
        client_a = None
        client_b = None
        try:
            server, server_log = launch("signalling-server", ["--server"])
            wait_for_port(port, 10, processes)

            client_a, client_a_log = launch(
                "client-a",
                ["--room", "integration-room", "--directory", str(receive_dir)],
            )
            wait_for_log(
                client_a_log,
                lambda content: '"type":"peer_joined"' in content and '"client-a"' in content,
                15,
                [server, client_a],
            )

            client_b, client_b_log = launch(
                "client-b",
                ["--room", "integration-room", "--peer", "client-a", "--file", str(source_file)],
            )

            def transfer_observed(content):
                received_sizes = [
                    int(size)
                    for size in re.findall(
                        r'Binary message peer "client-b" received, size=(\d+)', content
                    )
                ]
                return sum(received_sizes) == len(payload)

            wait_for_log(client_a_log, transfer_observed, 60, [server, client_a, client_b])
            wait_for_log(
                client_a_log,
                lambda content: "File transfer to 'client-b' in room 'integration-room' completed." in content,
                10,
                [server, client_a, client_b],
            )

            received_sizes = [
                int(size)
                for size in re.findall(
                    r'Binary message peer "client-b" received, size=(\d+)',
                    client_a_log.read_text(errors="replace"),
                )
            ]
            if sum(received_sizes) != len(payload):
                raise AssertionError(
                    f"Received {sum(received_sizes)} of {len(payload)} payload bytes"
                )
            print(
                f"Transferred {len(payload)} random bytes in {len(received_sizes)} "
                "data-channel message(s); completion was signalled."
            )
        except Exception:
            for log_path in root.glob("*.log"):
                print(f"\n--- {log_path.name} ---", file=sys.stderr)
                print(log_path.read_text(errors="replace"), file=sys.stderr)
            raise
        finally:
            stop_process(client_b)
            stop_process(client_a)
            stop_process(server)
            for log_file in log_files:
                log_file.close()


if __name__ == "__main__":
    main()