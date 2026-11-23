#!/usr/bin/env python3
# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

"""Exercise the installed controller with the real UDP lockstep client."""

import argparse
import csv
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def until(predicate, seconds=4):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.02)
    raise AssertionError("Expected controller state was not observed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bin", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--controller", type=Path)
    parser.add_argument("--qemu-sysroot", type=Path)
    args = parser.parse_args()
    args.bin = args.bin.resolve()
    suffix = ".exe" if os.name == "nt" else ""
    with tempfile.TemporaryDirectory(prefix="seabalance-link-") as temp:
        root = Path(temp)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        state = root / "persistent"
        status = root / "panel"
        service = args.controller or args.bin / ("slx-seabalance-controller" + suffix)
        start = [str(service.resolve()), "--bind", "127.0.0.1", "--port", str(port),
                 "--state", str(state), "--status", str(status)]
        if args.qemu_sysroot:
            start = ["qemu-arm", "-L", str(args.qemu_sysroot), *start]
        cli = args.bin / ("slx-seabalance-cli" + suffix)

        def panel():
            try:
                return json.loads((status / "status.json").read_text(encoding="utf-8"))
            except (OSError, ValueError):
                return {}

        def command(name, *options, timing="lockstep", seconds="4"):
            return [str(cli), "run", "--output", str(root / name), "--mode", "pid",
                    "--timing", timing, "--peer", "127.0.0.1", "--port", str(port),
                    "--seconds", seconds, "--warmup", "0", *options]

        def run(*options, expected=0, **kwargs):
            done = subprocess.run(command(*options, **kwargs), capture_output=True, text=True, timeout=18)
            assert done.returncode == expected, (done.returncode, done.stdout, done.stderr)
            return json.loads(done.stdout)

        def stop(proc):
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(3)

        controller = subprocess.Popen(start, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        host = None
        try:
            initial = until(lambda: panel().get("boot"))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as bad:
                bad.sendto(b"SLXB", ("127.0.0.1", port))
                bad.sendto(b"S" * 180, ("127.0.0.1", port))
            until(lambda: panel().get("invalid_frames", 0) >= 2)
            base = run("base")
            assert base["manifest"]["rows"] == 200 and base["metrics"]["rtt_p95_ms"] is not None
            loss = run("lost", "--fault", "link-loss", "--fault-at", "1", expected=2)
            assert loss["manifest"]["rows"] == 50 and loss["manifest"]["status"] == "aborted"
            until(lambda: panel().get("cause") == 4 and not panel().get("connected", True))
            recovery = run("recovery", "--ack-at", "1")
            with (root / "recovery/events.csv").open(encoding="utf-8") as events:
                next(events)
                lines = list(csv.DictReader(events))
            assert any(item["event"] == "alarm_acknowledged" for item in lines)
            until(lambda: panel().get("cause") == 0 and not panel().get("connected", True))
            paced = run("timed", timing="timed", seconds="3")
            assert paced["manifest"]["wall_elapsed_s"] >= 2.90
            assert paced["metrics"]["release_jitter_p95_ms"] is not None

            host = subprocess.Popen(command("restart-cut", timing="timed", seconds="7"),
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            until(lambda: panel().get("connected") and panel().get("sequence", 0) > 12)
            controller.kill()
            controller.wait(3)
            out, err = host.communicate(timeout=6)
            assert host.returncode == 2, (out, err)
            assert json.loads(out)["manifest"]["status"] == "aborted"
            controller = subprocess.Popen(start, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            until(lambda: panel().get("boot") != initial and panel().get("cause") == 5)
            restarted = run("restart-recovery", "--ack-at", "1")
            until(lambda: panel().get("last_alarm") == 5 and panel().get("cause") == 0)
            evidence = {
                "copyright": "Copyright (c) 2026 Marco Pezzullo (Silicon LogiX)",
                "result": "pass", "platform_id": base["manifest"]["peer_platform"],
                "frames_per_second": 50, "lockstep": base["metrics"], "timed": paced["metrics"],
                "packet_loss": {"responded": loss["manifest"]["rows"], "retained_alarm": 4},
                "recovery": recovery["manifest"]["status"],
                "process_restart": {"retained_alarm": 5, "recovery": restarted["manifest"]["status"]},
            }
            if args.output:
                args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
            print(json.dumps(evidence, indent=2))
        finally:
            if host:
                stop(host)
            stop(controller)


if __name__ == "__main__":
    main()
