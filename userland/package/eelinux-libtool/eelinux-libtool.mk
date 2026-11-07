EELINUX_LIBTOOL_VERSION = 2.4.6
EELINUX_LIBTOOL_SOURCE = libtool-$(EELINUX_LIBTOOL_VERSION).tar.xz
EELINUX_LIBTOOL_SITE = https://ftp.gnu.org/gnu/libtool
EELINUX_LIBTOOL_LICENSE = GPL-2.0+, LGPL-2.1+
EELINUX_LIBTOOL_LICENSE_FILES = COPYING libltdl/COPYING.LIB
EELINUX_LIBTOOL_DEPENDENCIES = eelinux-autoconf eelinux-automake host-m4
EELINUX_LIBTOOL_CONF_ENV = MAKEINFO=true
EELINUX_LIBTOOL_CONF_OPTS = --enable-ltdl-install
# Buildroot's standard libtool package deliberately installs only libltdl.
# This package installs the native build scripts too; remove workstation paths.
define EELINUX_LIBTOOL_NATIVE_COMPILER
	$(SED) 's|$(TARGET_CC)|/usr/bin/gcc|g' $(TARGET_DIR)/usr/bin/libtool
	$(SED) 's|$(TARGET_CXX)|/usr/bin/g++|g' $(TARGET_DIR)/usr/bin/libtool
	$(SED) 's|$(TARGET_AR)|/usr/bin/ar|g' $(TARGET_DIR)/usr/bin/libtool
	$(SED) 's|$(TARGET_LD)|/usr/bin/ld|g' $(TARGET_DIR)/usr/bin/libtool
	$(SED) 's|$(STAGING_DIR)||g' $(TARGET_DIR)/usr/bin/libtool
endef
EELINUX_LIBTOOL_POST_INSTALL_TARGET_HOOKS += EELINUX_LIBTOOL_NATIVE_COMPILER
$(eval $(autotools-package))
