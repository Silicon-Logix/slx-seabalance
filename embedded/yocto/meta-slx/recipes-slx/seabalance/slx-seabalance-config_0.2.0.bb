# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

SUMMARY = "SLX SeaBalance configuration, notices and journal limits"
LICENSE = "CLOSED"
COMPATIBLE_MACHINE = "^stm32mp13-disco$"
SRC_URI = "file://slx-seabalance-sdk-${PV}.tar.gz file://seabalance.env file://60-slx-seabalance.conf"
S = "${WORKDIR}/slx-seabalance-${PV}"

do_install() {
    install -D -m 0644 ${WORKDIR}/seabalance.env ${D}${sysconfdir}/slx-seabalance/service.env
    install -D -m 0644 ${WORKDIR}/60-slx-seabalance.conf ${D}${sysconfdir}/systemd/journald.conf.d/60-slx-seabalance.conf
    install -D -m 0644 ${S}/LICENSE ${D}${datadir}/slx-seabalance/LICENSE
    install -D -m 0644 ${S}/licenses/json-LICENSE.MIT ${D}${datadir}/slx-seabalance/json-LICENSE.MIT
    install -D -m 0644 ${S}/assets/branding/SLX_Official_Logo.png ${D}${datadir}/slx-seabalance/SLX_Official_Logo.png
    install -D -m 0644 ${S}/assets/ASSET_NOTICE.txt ${D}${datadir}/slx-seabalance/ASSET_NOTICE.txt
}

FILES:${PN} += "${datadir}/slx-seabalance"
CONFFILES:${PN} += "${sysconfdir}/slx-seabalance/service.env"
