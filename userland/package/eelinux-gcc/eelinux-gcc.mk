################################################################################
# Native GNU GCC. This is a Canadian cross: build is the Linux workstation,
# host and target are R5900 Linux. Never run the new target compiler on the host.
################################################################################

EELINUX_GCC_VERSION = 16.2.0
EELINUX_GCC_SOURCE = gcc-$(EELINUX_GCC_VERSION).tar.xz
EELINUX_GCC_SITE = https://ftp.gnu.org/gnu/gcc/gcc-$(EELINUX_GCC_VERSION)
EELINUX_GCC_LICENSE = GPL-3.0+, GPL-3.0+ with GCC Runtime Library Exception
EELINUX_GCC_LICENSE_FILES = COPYING3 COPYING.RUNTIME
EELINUX_GCC_DEPENDENCIES = gmp mpfr mpc zlib eelinux-binutils host-bison host-flex

define EELINUX_GCC_CONFIGURE_CMDS
	mkdir -p $(@D)/build
	cd $(@D)/build && $(TARGET_CONFIGURE_OPTS) \
		CC_FOR_BUILD="$(HOSTCC)" CXX_FOR_BUILD="$(HOSTCXX)" \
		CC_FOR_TARGET="$(TARGET_CC)" CXX_FOR_TARGET="$(TARGET_CXX)" \
		CFLAGS_FOR_TARGET="$(TARGET_CFLAGS)" \
		CXXFLAGS_FOR_TARGET="$(TARGET_CXXFLAGS)" \
		../configure \
		--build=$(GNU_HOST_NAME) --host=$(TOOLCHAIN_EXTERNAL_PREFIX) \
		--target=$(TOOLCHAIN_EXTERNAL_PREFIX) --prefix=/usr \
		--with-sysroot=/ --with-build-sysroot=$(STAGING_DIR) \
		--with-arch=r5900 --with-abi=32 --with-float=soft --with-endian=little \
		--with-gmp=$(STAGING_DIR)/usr --with-mpfr=$(STAGING_DIR)/usr \
		--with-mpc=$(STAGING_DIR)/usr --with-system-zlib \
		--enable-languages=c,c++ --enable-__cxa_atexit \
		--disable-bootstrap --disable-multilib --disable-nls \
		--disable-libsanitizer --disable-libgomp --disable-libssp \
		--disable-libvtv --disable-libquadmath --disable-libstdcxx \
		--disable-lto --disable-plugin --disable-werror --without-isl \
		--without-zstd --with-pkgversion="EELINUX native R5900 GCC"
endef

define EELINUX_GCC_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/build all-gcc
endef

define EELINUX_GCC_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/build \
		DESTDIR=$(TARGET_DIR) install-gcc
	ln -sf gcc $(TARGET_DIR)/usr/bin/cc
	ln -sf g++ $(TARGET_DIR)/usr/bin/c++
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_EELINUX_PATH)/configs/native-gcc.specs \
		$(TARGET_DIR)/usr/lib/gcc/$(TOOLCHAIN_EXTERNAL_PREFIX)/$(EELINUX_GCC_VERSION)/specs
endef

$(eval $(generic-package))
