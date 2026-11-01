# See https://git.yoctoproject.org/poky/tree/meta/files/common-licenses
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# TODO: Set this  with the path to your assignments repo.  Use https protocol and a public
# repo, or see assignment instructions for use with ssh keys
SRC_URI = "git://github.com/KHADGA-R/aeld-assignment-3-and-later.git;protocol=https;branch=main"

PV = "1.0+git${SRCPV}"
# TODO: set to reference a specific commit hash in your assignment repo
SRCREV = "97673024b4f1ae4cf6a496db55dc318185182af4"

# TODO: Add the aesdsocket application and any other files you will install in do_install below
# See https://github.com/openembedded/openembedded-core/blob/wrynose/meta/conf/bitbake.conf for path prefixes like ${bindir}
# or ${sysconfdir}"
#FILES:${PN} += "${bindir}/aesdsocket"

# TODO: customize these as necessary for any libraries you need for your application
#TARGET_LDFLAGS += "-pthread -lrt"

do_configure () {
	:
}

do_compile () {
    # TODO: switch to the server directory where your source code to be built is located
    oe_runmake -C ${S}/server clean
    oe_runmake -C ${S}/server CC="${CC}" \
        CFLAGS="${CFLAGS} -pthread" \
        LDFLAGS="${LDFLAGS}"
}

#TODO: add initscript necessary changes here
inherit update-rc.d

INITSCRIPT_NAME = "aesdsocket"
INITSCRIPT_PARAMS = "defaults 99"


do_install () {
	# TODO: Install your binaries/scripts here.
	# Be sure to install the target directory with install -d first
	# Yocto variables ${D} and ${S} are useful here, which you can read about at 
	# https://docs.yoctoproject.org/ref-manual/variables.html?highlight=workdir#term-D
	# and
	# https://docs.yoctoproject.org/ref-manual/variables.html?highlight=workdir#term-S
	# See examples at https://github.com/cu-ecen-aeld/yocto-hello-world for your relevant build as well
	# Remember to copy files relative to their location after building, which may be under the server subdirectory

    install -d ${D}${bindir}
    install -m 0755 ${S}/server/aesdsocket ${D}${bindir}/aesdsocket

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/server/aesdsocket-start-stop ${D}${sysconfdir}/init.d/aesdsocket
}

