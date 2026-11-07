################################################################################
# Maintained OpenSSL 3 ABI alongside the current OpenSSL release.
################################################################################
EELINUX_OPENSSL3_COMPAT_VERSION = 3.5.9
EELINUX_OPENSSL3_COMPAT_SOURCE = openssl-$(EELINUX_OPENSSL3_COMPAT_VERSION).tar.gz
EELINUX_OPENSSL3_COMPAT_SITE = https://github.com/openssl/openssl/releases/download/openssl-$(EELINUX_OPENSSL3_COMPAT_VERSION)
EELINUX_OPENSSL3_COMPAT_LICENSE = Apache-2.0
EELINUX_OPENSSL3_COMPAT_LICENSE_FILES = LICENSE.txt
EELINUX_OPENSSL3_COMPAT_INSTALL_STAGING = YES
EELINUX_OPENSSL3_COMPAT_DEPENDENCIES = libopenssl zlib

# Configure a distinct provider/engine and configuration namespace. Only the
# versioned ABI libraries are also exposed in the default runtime search path.
define EELINUX_OPENSSL3_COMPAT_CONFIGURE_CMDS
	cd $(@D) && $(TARGET_CONFIGURE_ARGS) $(TARGET_CONFIGURE_OPTS) \
		./Configure $(call qstrip,$(BR2_PACKAGE_LIBOPENSSL_TARGET_ARCH)) \
		--prefix=/usr --libdir=lib/openssl3 --openssldir=/etc/ssl/openssl3 \
		shared threads zlib-dynamic enable-camellia no-rc5 no-apps \
		no-docs no-tests no-fuzz-libfuzzer no-fuzz-afl no-afalgeng \
		$(if $(BR2_TOOLCHAIN_HAS_UCONTEXT),,-DOPENSSL_NO_ASYNC)
endef

define EELINUX_OPENSSL3_COMPAT_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define EELINUX_OPENSSL3_COMPAT_INSTALL_TREE
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(@D)/eelinux-install-root install_sw
	$(INSTALL) -d -m 0755 $(1)/usr/lib/openssl3 $(1)/usr/include/openssl3 \
		$(1)/usr/lib/pkgconfig $(1)/etc/ssl/openssl3
	cp -af $(@D)/eelinux-install-root/usr/lib/openssl3/. $(1)/usr/lib/openssl3/
	cp -af $(@D)/eelinux-install-root/usr/include/openssl $(1)/usr/include/openssl3/
	$(INSTALL) -m 0755 $(@D)/eelinux-install-root/usr/lib/openssl3/libcrypto.so.3 $(1)/usr/lib/
	$(INSTALL) -m 0755 $(@D)/eelinux-install-root/usr/lib/openssl3/libssl.so.3 $(1)/usr/lib/
	for pc in $(1)/usr/lib/openssl3/pkgconfig/*.pc; do \
		sed -i 's|^includedir=.*|includedir=$${prefix}/include/openssl3|' "$$pc"; \
	done
	for pair in libcrypto:libcrypto3 libssl:libssl3 openssl:openssl3; do \
		old=$${pair%:*}; new=$${pair#*:}; \
		sed -e '/^Requires[:.]/s/\<libcrypto\>/libcrypto3/g' \
			-e '/^Requires[:.]/s/\<libssl\>/libssl3/g' \
			$(1)/usr/lib/openssl3/pkgconfig/$$old.pc >$(1)/usr/lib/pkgconfig/$$new.pc; \
	done
	$(INSTALL) -m 0644 $(@D)/apps/openssl.cnf $(1)/etc/ssl/openssl3/openssl.cnf
	ln -sf ../certs $(1)/etc/ssl/openssl3/certs
	ln -sf ../certs/ca-certificates.crt $(1)/etc/ssl/openssl3/cert.pem
endef

define EELINUX_OPENSSL3_COMPAT_INSTALL_STAGING_CMDS
	$(call EELINUX_OPENSSL3_COMPAT_INSTALL_TREE,$(STAGING_DIR))
endef

define EELINUX_OPENSSL3_COMPAT_INSTALL_TARGET_CMDS
	$(call EELINUX_OPENSSL3_COMPAT_INSTALL_TREE,$(TARGET_DIR))
endef

$(eval $(generic-package))
