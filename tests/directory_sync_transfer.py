#!/usr/bin/env python3
"""Two clients name each other with --peer and mirror files written to their data directories."""

import os
import pathlib
import shutil
import signal
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


def wait_for_log(log_path, text, timeout, processes):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if text in log_path.read_text(errors="replace"):
            return
        for process in processes:
            if process.poll() is not None:
                raise RuntimeError(f"CloudNode process {process.pid} exited unexpectedly")
        time.sleep(0.1)
    raise TimeoutError(f"Timed out waiting for '{text}' in {log_path.name}")


def stop_process(process):
    if process is None or process.poll() is not None:
        return
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def write_random_files(directory, count, size_base, prefix):
    """Creates random files under directory and returns {file name: bytes}."""
    expected = {}
    for index in range(count):
        path = directory / f"{prefix}-file-{index}.bin"
        payload = os.urandom(size_base + index * 4099 + 13)
        path.write_bytes(payload)
        expected[path.name] = payload
    return expected


def populate(root, label):
    """Writes top-level files plus files in a directory and a nested directory."""
    expected = write_random_files(root, 3, 70 * 1024, label)

    for subdir in (pathlib.Path(f"{label}-dir"), pathlib.Path(f"{label}-dir") / "nested"):
        (root / subdir).mkdir()
        time.sleep(0.5)  # allow the watcher to register the new directory
        for name, payload in write_random_files(root / subdir, 2, 40 * 1024, label).items():
            expected[(subdir / name).as_posix()] = payload
    return expected


def wait_for_files(directory, expected, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        missing = []
        for relative, payload in expected.items():
            path = directory / relative
            if not path.is_file() or path.read_bytes() != payload:
                missing.append(relative)
        if not missing:
            return
        time.sleep(0.25)
    raise AssertionError(f"Files did not arrive intact in {directory}: {sorted(missing)}")


def snapshot(directory):
    return {
        path.relative_to(directory).as_posix(): (path.stat().st_size, path.stat().st_mtime_ns)
        for path in directory.rglob("*")
        if path.is_file()
    }


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: directory_sync_transfer.py CLOUDNODE_EXECUTABLE")

    executable = str(pathlib.Path(sys.argv[1]).resolve())
    stdbuf = shutil.which("stdbuf")
    command_prefix = [stdbuf, "-oL", "-eL"] if stdbuf else []
    processes = []
    log_files = []
    with tempfile.TemporaryDirectory(prefix="cloudnode-dir-sync-") as temp_dir:
        root = pathlib.Path(temp_dir)
        dir_a = root / "a"
        dir_b = root / "b"
        dir_a.mkdir()
        dir_b.mkdir()
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

        server = client_a = client_b = None
        try:
            server, _ = launch("signalling-server", ["--server"])
            wait_for_port(port, 10, processes)

            client_a, log_a = launch(
                "client-a",
                ["--room", "sync-room", "--peer", "client-b", "--directory", str(dir_a)],
            )
            client_b, log_b = launch(
                "client-b",
                ["--room", "sync-room", "--peer", "client-a", "--directory", str(dir_b)],
            )
            wait_for_log(log_a, "Offering DataChannel open!", 30, processes)
            wait_for_log(log_b, "Offering DataChannel open!", 30, processes)

            expected_a = populate(dir_a, "from-a")
            wait_for_files(dir_b, expected_a, 60)

            expected_b = populate(dir_b, "from-b")
            wait_for_files(dir_a, expected_b, 60)

            # Received files must not be echoed back and forth.
            before = (snapshot(dir_a), snapshot(dir_b))
            time.sleep(5)
            after = (snapshot(dir_a), snapshot(dir_b))
            if before != after:
                raise AssertionError("Directory contents kept changing after the sync completed")
            for process in (server, client_a, client_b):
                if process.poll() is not None:
                    raise AssertionError(f"CloudNode process {process.pid} exited during the test")
        except BaseException:
            for log_file in log_files:
                log_file.flush()
                log_file.seek(0)
                print(f"----- {pathlib.Path(log_file.name).name} -----", file=sys.stderr)
                print(log_file.read(), file=sys.stderr)
            raise
        finally:
            for process in (client_b, client_a, server):
                stop_process(process)
            for log_file in log_files:
                log_file.close()

    print("PASS: files and directories synced between both clients")


if __name__ == "__main__":
    main()
