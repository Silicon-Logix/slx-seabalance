#!/usr/bin/env python3
# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

"""Install the SeaBalance layer and prepare its reproducible application sources."""

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tarfile

VERSION = "0.2.0"
SOURCES = (
    "CMakeLists.txt", "LICENSE", "cmake/standards.cmake",
    "sdk/include/slx/seabalance.h", "sdk/src/runtime.hpp", "sdk/src/runtime.cpp",
    "sdk/src/wire.hpp", "sdk/src/wire.cpp", "sdk/src/socket.cpp",
    "sdk/src/link.hpp", "sdk/src/link.cpp", "examples/pid.hpp", "examples/pid.cpp",
    "embedded/service.cpp", "embedded/panel_state.hpp", "embedded/panel_state.cpp",
    "embedded/panel.cpp", "third_party/include/nlohmann/json.hpp", "licenses/json-LICENSE.MIT",
    "assets/branding/SLX_Official_Logo.png", "assets/ASSET_NOTICE.txt",
)


def main():
    top = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layer", type=Path, required=True)
    parser.add_argument("--control-core", type=Path, required=True)
    args = parser.parse_args()
    core = args.control_core.read_bytes()
    if (len(core) < 52 or core[:6] != b"\x7fELF\x01\x01"
            or struct.unpack_from("<H", core, 18)[0] != 40
            or not struct.unpack_from("<I", core, 36)[0] & 0x400):
        parser.error("Expected an ARMv7 hard-float ELF shared library")
    shutil.copytree(top / "embedded/yocto/meta-slx", args.layer, dirs_exist_ok=True)
    files = args.layer / "recipes-slx/seabalance/files"
    archive = files / f"slx-seabalance-sdk-{VERSION}.tar.gz"
    with archive.open("wb") as raw, gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as zipped:
        with tarfile.open(fileobj=zipped, mode="w") as tar:
            for name in sorted(SOURCES):
                item = top / name
                meta = tar.gettarinfo(str(item), f"slx-seabalance-{VERSION}/{name}")
                meta.uid = meta.gid = meta.mtime = 0
                meta.uname = meta.gname = ""
                meta.mode = 0o644
                with item.open("rb") as contents:
                    tar.addfile(meta, contents)
    library = files / "libslx_seabalance_control.so"
    library.write_bytes(core)
    manifest = {
        "copyright": "Copyright (c) 2026 Marco Pezzullo (Silicon LogiX)",
        "bsp": "openstlinux-6.6-yocto-scarthgap-mpu-v26.06.10",
        "machine": "stm32mp13-disco",
        "inputs": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (archive, library)},
    }
    (args.layer / "SLX-INPUTS.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
