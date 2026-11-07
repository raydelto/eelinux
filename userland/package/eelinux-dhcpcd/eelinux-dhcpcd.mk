EELINUX_DHCPCD_VERSION = 10.5.2
EELINUX_DHCPCD_SOURCE = dhcpcd-$(EELINUX_DHCPCD_VERSION).tar.xz
EELINUX_DHCPCD_SITE = https://github.com/NetworkConfiguration/dhcpcd/releases/download/v$(EELINUX_DHCPCD_VERSION)
EELINUX_DHCPCD_LICENSE = BSD-2-Clause
EELINUX_DHCPCD_LICENSE_FILES = LICENSE
EELINUX_DHCPCD_CONF_OPTS = --prefix=/usr --sbindir=/sbin --sysconfdir=/etc \
	--libexecdir=/usr/lib/dhcpcd --mandir=/usr/share/man \
	--rundir=/run/dhcpcd --dbdir=/var/lib/dhcpcd \
	--privsepuser=dhcpcd --enable-privsep --disable-seccomp --disable-ipv6 \
	--without-dev --without-openssl --without-libpcap

define EELINUX_DHCPCD_CONFIGURE_CMDS
	(cd $(@D); $(TARGET_CONFIGURE_OPTS) ./configure \
		--host=$(GNU_TARGET_NAME) $(EELINUX_DHCPCD_CONF_OPTS) \
		CPPFLAGS="$(TARGET_CPPFLAGS) -D_FILE_OFFSET_BITS=64")
endef

define EELINUX_DHCPCD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

# eeLinux supplies its own Bash DNS hook and SysV network service. Install only
# the client, its manuals and license, without a second init service or hooks.
define EELINUX_DHCPCD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/src/dhcpcd $(TARGET_DIR)/sbin/dhcpcd
	$(INSTALL) -D -m 0644 $(@D)/src/dhcpcd.8 $(TARGET_DIR)/usr/share/man/man8/dhcpcd.8
	$(INSTALL) -D -m 0644 $(@D)/src/dhcpcd.conf.5 $(TARGET_DIR)/usr/share/man/man5/dhcpcd.conf.5
	$(INSTALL) -D -m 0644 $(@D)/LICENSE $(TARGET_DIR)/usr/share/licenses/dhcpcd/LICENSE
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/var/lib/dhcpcd
endef

define EELINUX_DHCPCD_USERS
	dhcpcd 101 dhcpcd 101 * /var/lib/dhcpcd /sbin/nologin - DHCP client privilege separation
endef

$(eval $(generic-package))
