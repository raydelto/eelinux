################################################################################
# Native GNU Binutils. Build is the workstation; host/target are R5900 Linux.
################################################################################

EELINUX_BINUTILS_VERSION = 2.47
EELINUX_BINUTILS_SOURCE = binutils-$(EELINUX_BINUTILS_VERSION).tar.xz
EELINUX_BINUTILS_SITE = https://ftp.gnu.org/gnu/binutils
EELINUX_BINUTILS_LICENSE = GPL-3.0+, GPL-2.0+, LGPL-2.1+
EELINUX_BINUTILS_LICENSE_FILES = COPYING COPYING3 COPYING.LIB
EELINUX_BINUTILS_DEPENDENCIES = zlib host-bison host-flex
EELINUX_BINUTILS_CONF_OPTS = \
	--host=$(TOOLCHAIN_EXTERNAL_PREFIX) \
	--target=$(TOOLCHAIN_EXTERNAL_PREFIX) \
	--with-sysroot=/ \
	--disable-multilib --disable-werror --disable-nls \
	--disable-gdb --disable-gdbserver --disable-sim --disable-gprofng \
	--disable-plugins --enable-install-libiberty \
	--with-system-zlib --without-zstd --without-debuginfod
EELINUX_BINUTILS_CONF_ENV = MAKEINFO=true
EELINUX_BINUTILS_MAKE_ENV = $(TARGET_CONFIGURE_ARGS)
EELINUX_BINUTILS_MAKE_OPTS = MAKEINFO=true
EELINUX_BINUTILS_INSTALL_TARGET_OPTS = DESTDIR=$(TARGET_DIR) MAKEINFO=true install

$(eval $(autotools-package))
