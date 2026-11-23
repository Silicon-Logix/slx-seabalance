# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

SUMMARY = "SLX SeaBalance Wayland operator panel (480x272)"
require slx-seabalance-common.inc
SRC_URI += "file://20-slx-seabalance-panel"
inherit pkgconfig
DEPENDS += "gtk+3"
RDEPENDS:${PN} += "slx-seabalance-service weston-init"
EXTRA_OECMAKE += "-DSLX_BUILD_SERVICE=OFF -DSLX_BUILD_HMI=ON"

do_install() {
    install -D -m 0755 ${B}/bin/slx-seabalance-panel ${D}${bindir}/slx-seabalance-panel
    install -D -m 0755 ${WORKDIR}/20-slx-seabalance-panel ${D}${datadir}/weston-start-at-startup/20-slx-seabalance-panel
}

FILES:${PN} += "${datadir}/weston-start-at-startup/20-slx-seabalance-panel"
