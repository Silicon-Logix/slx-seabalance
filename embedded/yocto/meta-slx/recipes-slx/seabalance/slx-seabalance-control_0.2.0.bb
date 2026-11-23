# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

SUMMARY = "SLX SeaBalance evaluation controller binary for ARMv7 hard-float"
LICENSE = "CLOSED"
COMPATIBLE_MACHINE = "^stm32mp13-disco$"
COMPATIBLE_HOST = "arm.*-linux-gnueabi"
PACKAGE_ARCH = "${TUNE_PKGARCH}"
SRC_URI = "file://libslx_seabalance_control.so"
S = "${WORKDIR}"

do_install() {
    install -D -m 0755 ${WORKDIR}/libslx_seabalance_control.so ${D}${libdir}/slx-seabalance/libslx_seabalance_control.so
}

FILES:${PN} += "${libdir}/slx-seabalance/libslx_seabalance_control.so"
INSANE_SKIP:${PN} += "already-stripped"
