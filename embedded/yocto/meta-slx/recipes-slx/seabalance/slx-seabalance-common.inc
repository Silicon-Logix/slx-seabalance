# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

LICENSE = "CLOSED"
COMPATIBLE_MACHINE = "^stm32mp13-disco$"
SRC_URI = "file://slx-seabalance-sdk-${PV}.tar.gz"
S = "${WORKDIR}/slx-seabalance-${PV}"

inherit cmake
EXTRA_OECMAKE = "-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSLX_BUILD_PC=OFF -DSLX_BUILD_DESKTOP=OFF"
