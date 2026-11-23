# Silicon LogiX / SLX SeaBalance
# Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
# Author: Marco Pezzullo
# License: Silicon LogiX Evaluation License 1.0. See LICENSE.

require recipes-st/images/st-image-weston.bb
SUMMARY = "SLX SeaBalance appliance for STM32MP135F-DK / OpenSTLinux Scarthgap"
COMPATIBLE_MACHINE = "^stm32mp13-disco$"

IMAGE_FEATURES:remove = "debug-tweaks tools-profile eclipse-debug hwcodecs ssh-server-dropbear"
CORE_IMAGE_EXTRA_INSTALL:remove = "packagegroup-st-demo packagegroup-framework-tools packagegroup-framework-tools-base packagegroup-framework-core-extra packagegroup-optee-test"
IMAGE_INSTALL:append = " slx-seabalance-service slx-seabalance-panel slx-seabalance-control slx-seabalance-config"
TOOLCHAIN_TARGET_TASK:append = " gtk+3-dev"

# ST's split-partition task moves files out of rootfs/boot while it runs.
do_image_wic[depends] += "${PN}:do_image_stsplitpartitions"
