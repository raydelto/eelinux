EELINUX_BISON_VERSION = 3.8.2
EELINUX_BISON_SOURCE = bison-$(EELINUX_BISON_VERSION).tar.xz
EELINUX_BISON_SITE = https://ftp.gnu.org/gnu/bison
EELINUX_BISON_LICENSE = GPL-3.0+
EELINUX_BISON_LICENSE_FILES = COPYING
EELINUX_BISON_DEPENDENCIES = eelinux-m4 host-m4
EELINUX_BISON_CONF_ENV = ac_cv_path_M4=/usr/bin/m4 ac_cv_libtextstyle=no
# Upstream examples/c/reccalc has a parallel-build race.
EELINUX_BISON_MAKE = $(MAKE1)
$(eval $(autotools-package))
