# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

SUMMARY = "SLX SeaBalance UDP controller for STM32MP135F-DK"
require slx-seabalance-common.inc
SRC_URI += "file://slx-seabalance.service"
EXTRA_OECMAKE += "-DSLX_BUILD_SERVICE=ON -DSLX_BUILD_HMI=OFF"

inherit useradd systemd
USERADD_DEPENDS = "weston-init"
RDEPENDS:${PN} += "slx-seabalance-control slx-seabalance-config weston-init"
USERADD_PACKAGES = "${PN}"
USERADD_PARAM:${PN} = "--system --no-create-home --home /var/lib/slx-seabalance --shell /sbin/nologin --gid weston seabalance"
SYSTEMD_SERVICE:${PN} = "slx-seabalance.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -D -m 0755 ${B}/bin/slx-seabalance-controller ${D}${bindir}/slx-seabalance-controller
    install -D -m 0644 ${WORKDIR}/slx-seabalance.service ${D}${systemd_system_unitdir}/slx-seabalance.service
    sed -i -e 's,@BINDIR@,${bindir},g' -e 's,@LIBDIR@,${libdir},g' ${D}${systemd_system_unitdir}/slx-seabalance.service
}
