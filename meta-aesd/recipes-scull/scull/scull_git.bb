# Recipe created by recipetool
# This is the basis of a recipe and may need further editing in order to be fully functional.
# (Feel free to remove these comments when editing.)

# WARNING: the following LICENSE and LIC_FILES_CHKSUM values are best guesses - it is
# your responsibility to verify that the values are complete and correct.
#
# The following license files were not able to be identified and are
# represented as "Unknown" below, you will need to check them yourself:
#   LICENSE
LICENSE = "LicenseRef-LDD3"
LIC_FILES_CHKSUM = "file://LICENSE;md5=f098732a73b5f6f3430472f5b094ffdb"
NO_GENERIC_LICENSE[LicenseRef-LDD3] = "LICENSE"

SRC_URI = "git://github.com/KHADGA-R/aeld-assignment-7.git;protocol=https;branch=main"
SRC_URI += "file://scull-init"

# Modify these as desired
PV = "1.0+git"
SRCREV = "acd836ee19e41116b8e3d0d316105d13721a7cf6"

inherit module update-rc.d
INITSCRIPT_NAME = "scull-init"
FILES:${PN} += "${sysconfdir}/init.d/scull-init"
INITSCRIPT_PARAMS = "start 99 2 3 4 5 . stop 01 0 1 6 ."
RDEPENDS:${PN} += "kernel-module-scull"

EXTRA_OEMAKE:append:task-compile = " -C ${STAGING_KERNEL_DIR} M=${S}/scull"
EXTRA_OEMAKE:append:task-install = " -C ${STAGING_KERNEL_DIR} M=${S}/scull"
EXTRA_OEMAKE += "KERNELDIR=${STAGING_KERNEL_DIR}"

do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${UNPACKDIR}/scull-init ${D}${sysconfdir}/init.d/scull-init
}
