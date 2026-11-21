include $(sort $(wildcard $(BR2_EXTERNAL_EELINUX_PATH)/package/*/*.mk))

# GNU Coreutils omits hostname unless explicitly requested. The runtime uses
# it to set /etc/hostname.
COREUTILS_CONF_OPTS += --enable-install-program=hostname

# glibc 2.44 implements bsearch as a macro in some builds. Keep the lsfd
# search key outside that macro's argument list so initializer commas are safe.
define EELINUX_UTIL_LINUX_LSFD_BSEARCH
	printf '%s  %s\n' 0680636e21ce834ad048144a00ac8edf356e661485251a24129f181acb43fee9 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/util-linux-2.40.4-lsfd-bsearch.patch" | sha256sum -c -
	printf '%s  %s\n' 25b92e0a4517b59c1f71d03f9877c32996a391aa2f586c327c35109d9a282e08 \
		"$(@D)/misc-utils/lsfd.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/util-linux-2.40.4-lsfd-bsearch.patch"
	printf '%s  %s\n' cfbf211edb5adaed0e61a17827921543d83f21072fbeea03354021195564633a \
		"$(@D)/misc-utils/lsfd.c" | sha256sum -c -
endef
ifeq ($(UTIL_LINUX_VERSION),2.40.4)
UTIL_LINUX_POST_PATCH_HOOKS += EELINUX_UTIL_LINUX_LSFD_BSEARCH
endif

# Preserve the read-only ioctl table when glibc's bsearch retains qualifiers.
define EELINUX_STRACE_CONST_BSEARCH
	printf '%s  %s\n' aeda0e1eb3fed82cdf82ef26b58cec2a535cd34f273032dc5d86d3e29f627b17 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/strace-6.13-const-bsearch.patch" | sha256sum -c -
	printf '%s  %s\n' 416cad67fffef481e3c9d347f0e8d9917476974332c7513b2b5db68ed76e6a6d \
		"$(@D)/src/ioctl.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/strace-6.13-const-bsearch.patch"
	printf '%s  %s\n' fec4626a0a82de834a863240c0f2b05296127db0f38ec240472d3f468b86e231 \
		"$(@D)/src/ioctl.c" | sha256sum -c -
endef
ifeq ($(STRACE_VERSION),6.13)
STRACE_POST_PATCH_HOOKS += EELINUX_STRACE_CONST_BSEARCH
endif

# Buildroot's CMake recipe otherwise installs only unzip. Keep the upstream
# streaming filter, self-extractor and ZipInfo listing entry point available.
define EELINUX_UNZIP_FULL_TOOLS
	printf '%s  %s\n' f28cfbca29ea46ccd5a85d661323b7942b2a7e39d53cca97b2ba82d75f26df2f \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/unzip-6.0-full-tools.patch" | sha256sum -c -
	printf '%s  %s\n' 3608cd7d6578f889fb8209401cacc11bfaa0f83fd6c1347a2fb0b66466ba7c86 \
		"$(@D)/CMakeLists.txt" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/unzip-6.0-full-tools.patch"
	printf '%s  %s\n' 31f4759fb375b6c5bdb438c5b816e4affd0315a78d6def018011a966708b2cbb \
		"$(@D)/CMakeLists.txt" | sha256sum -c -
endef
UNZIP_POST_PATCH_HOOKS += EELINUX_UNZIP_FULL_TOOLS
define EELINUX_UNZIP_ZIPINFO_ALIAS
	ln -sf unzip $(TARGET_DIR)/usr/bin/zipinfo
endef
UNZIP_POST_INSTALL_TARGET_HOOKS += EELINUX_UNZIP_ZIPINFO_ALIAS

# Networking programs are available for explicit administration. eeLinux's
# runlevel configuration owns services; package selection must not enable
# servers, generate host keys or step the clock during startup.
OPENSSH_INSTALL_INIT_SYSV =
OPENSSH_INSTALL_INIT_SYSTEMD =
OPENVPN_INSTALL_INIT_SYSV =
OPENVPN_INSTALL_INIT_SYSTEMD =
ATFTP_INSTALL_INIT_SYSV =
ATFTP_INSTALL_INIT_SYSTEMD =
TFTPD_INSTALL_INIT_SYSV =
TFTPD_INSTALL_INIT_SYSTEMD =
IPTABLES_INSTALL_INIT_SYSV =
IPTABLES_INSTALL_INIT_SYSTEMD =
NTP_INSTALL_INIT_SYSV =
NTP_INSTALL_INIT_SYSTEMD =
BIND_INSTALL_INIT_SYSV =
BIND_INSTALL_INIT_SYSTEMD =
WPA_SUPPLICANT_INSTALL_INIT_SYSV =
WPA_SUPPLICANT_INSTALL_INIT_SYSTEMD =
RPCBIND_INSTALL_INIT_SYSV =
RPCBIND_INSTALL_INIT_SYSTEMD =
NFS_UTILS_INSTALL_INIT_SYSV =
NFS_UTILS_INSTALL_INIT_SYSTEMD =
RSYNC_INSTALL_INIT_SYSV =
RSYNC_INSTALL_INIT_SYSTEMD =
UNBOUND_INSTALL_INIT_SYSV =
UNBOUND_INSTALL_INIT_SYSTEMD =

# Retain the command-line scripting engine and its complete scripts/data.
# The stock recipe selects only the individual program install targets.
ifeq ($(BR2_PACKAGE_NMAP_NMAP),y)
NMAP_CONF_OPTS := $(filter-out --without-liblua,$(NMAP_CONF_OPTS)) --with-liblua=included
NMAP_INSTALL_TARGET_OPTS += install-nse
endif

define EELINUX_NMAP_LUA_CROSS_TOOLS
	printf '%s  %s\n' ba897b65864ee75dd592d116ac00338212c39da85dcaf267af9c5808c5982517 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/nmap-7.95-lua-cross-tools.patch" | sha256sum -c -
	printf '%s  %s\n' 1dfeb7cd11c82b796bdd0af8707c3205b71ab4484d0a7dea4a122593b2eb3deb \
		"$(@D)/Makefile.in" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/nmap-7.95-lua-cross-tools.patch"
	printf '%s  %s\n' ef84e4961e56df914d063c6accf2fd38edc53e9172a121d7c2da1492a2ef2641 \
		"$(@D)/Makefile.in" | sha256sum -c -
endef
ifeq ($(NMAP_VERSION),7.95)
NMAP_POST_PATCH_HOOKS += EELINUX_NMAP_LUA_CROSS_TOOLS
endif

# Include the native DNS clients and DNSSEC inspection utilities. These
# implementations do not require legacy __sync primitives unavailable on EE.
LIBLDNS_CONF_OPTS := $(filter-out --without-examples,$(LIBLDNS_CONF_OPTS)) --with-drill --with-examples
LIBLDNS_DEPENDENCIES += libpcap
LIBLDNS_MAKE_OPTS = lib drill examples
define EELINUX_C_ARES_CLIENT_TOOLS
	for program in adig ahost; do \
		binary="$(@D)/src/tools/.libs/$$program"; \
		if ! test -f "$$binary"; then binary="$(@D)/src/tools/$$program"; fi; \
		$(TARGET_READELF) -h "$$binary" >/dev/null || exit 1; \
		$(INSTALL) -D -m 0755 "$$binary" "$(TARGET_DIR)/usr/bin/$$program" || exit 1; \
	done
endef
ifneq ($(C_ARES_INTEGRATED_TOOLS),YES)
C_ARES_POST_INSTALL_TARGET_HOOKS += EELINUX_C_ARES_CLIENT_TOOLS
endif

# Keep GNU's hostname selected while retaining the net-tools implementation
# and its DNS/NIS domain-name entry points under unambiguous names.
define EELINUX_NET_TOOLS_HOSTNAME
	$(INSTALL) -D -m 0755 $(@D)/hostname $(TARGET_DIR)/usr/bin/net-hostname
	for directory in $(TARGET_DIR)/bin $(TARGET_DIR)/usr/bin; do \
		for name in dnsdomainname domainname ypdomainname nisdomainname nodename; do \
			if test -L "$$directory/$$name"; then \
				ln -sf /usr/bin/net-hostname "$$directory/$$name"; \
			fi; \
		done; \
	done
	ln -sf ../usr/bin/coreutils $(TARGET_DIR)/bin/hostname
	ln -sf coreutils $(TARGET_DIR)/usr/bin/hostname
endef
NET_TOOLS_POST_INSTALL_TARGET_HOOKS += EELINUX_NET_TOOLS_HOSTNAME

# Keep true/false available without changing ncurses' generated bool ABI.
define EELINUX_IPTRAF_STANDARD_BOOL
	printf '%s  %s\n' 06d1436597580812e747991e6be9641c9c86815ee82866822d1247126294eae2 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/iptraf-ng-1.2.2-stdbool.patch" | sha256sum -c -
	printf '%s  %s\n' 0ae71baf06c6bced154bf0b8be77a8afd0d0b4a5431b48ede2b1e47d2672c08e \
		"$(@D)/src/iptraf-ng-compat.h" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/iptraf-ng-1.2.2-stdbool.patch"
	printf '%s  %s\n' f099782cce0f26b9773c217bda3217c688407720e1259a84b4620113b2efcf91 \
		"$(@D)/src/iptraf-ng-compat.h" | sha256sum -c -
endef
ifeq ($(IPTRAF_NG_VERSION),1.2.2)
IPTRAF_NG_POST_PATCH_HOOKS += EELINUX_IPTRAF_STANDARD_BOOL
endif

# The optional command filter is a pointer; pass an explicit boolean to htop's
# panel API while preserving ncurses' configured public boolean ABI.
define EELINUX_HTOP_EXPLICIT_FILTER_BOOL
	printf '%s  %s\n' 65bca84a4358dcce8b727685ea3d50301744de5486c622eac0111ff69048d093 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/htop-3.3.0-explicit-filter-bool.patch" | sha256sum -c -
	printf '%s  %s\n' 2277dd832041c6e5bd6c887b86493fea31a0039f1d48d6ecbd85d4ed55277a57 \
		"$(@D)/CommandLine.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/htop-3.3.0-explicit-filter-bool.patch"
	printf '%s  %s\n' 25c7cbf14271a80af8ef32c16faae237c1d3d04ac33a445df6a01604a46fdfbb \
		"$(@D)/CommandLine.c" | sha256sum -c -
endef
ifeq ($(HTOP_VERSION),3.3.0)
HTOP_POST_PATCH_HOOKS += EELINUX_HTOP_EXPLICIT_FILTER_BOOL
endif

# Restore htop's _Bool/bool type only after curses' public declarations have
# been parsed, so include order cannot change htop's cross-file interfaces.
define EELINUX_HTOP_CURSES_BOOL_SCOPE
	printf '%s  %s\n' a449eb9baa59418cc510395e6452f825fcb731f4adcf1f5426d09b988632b744 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/htop-3.3.0-curses-bool-scope.patch" | sha256sum -c -
	printf '%s  %s\n' ef46089c24dc756ec8b94361dfe995ad216e78a005b85a40fd90e4bf5c16aece \
		"$(@D)/ProvideCurses.h" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/htop-3.3.0-curses-bool-scope.patch"
	printf '%s  %s\n' f6c407b825d2caaa0aaf291c73ebec26edc7f30c02333c96d23049fbd294bf1b \
		"$(@D)/ProvideCurses.h" | sha256sum -c -
endef
ifeq ($(HTOP_VERSION),3.3.0)
HTOP_POST_PATCH_HOOKS += EELINUX_HTOP_CURSES_BOOL_SCOPE
endif

# Current htop still relies on the configured curses header's integer boolean
# type staying out of its own C interfaces.
define EELINUX_HTOP_CURRENT_CURSES_BOOL_SCOPE
	printf '%s  %s\n' f8fefe3176bda50db7eea49c3606497ff4fa3cf94c39872e867730faaa1d2d7c \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/htop-3.5.3-curses-bool-scope.patch" | sha256sum -c -
	printf '%s  %s\n' 16100d92b722da467edc3bcbf4b2e1b20b767208061778f6c76c157a9db3cb4a \
		"$(@D)/ProvideCurses.h" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/htop-3.5.3-curses-bool-scope.patch"
	printf '%s  %s\n' e02ec2e851a9cb47180e9aaa8a5ee29e97bdd2ef2b5a300868acc5536562d11f \
		"$(@D)/ProvideCurses.h" | sha256sum -c -
endef
ifeq ($(HTOP_VERSION),3.5.3)
HTOP_POST_PATCH_HOOKS += EELINUX_HTOP_CURRENT_CURSES_BOOL_SCOPE
endif

# Keep Expect's terminal helper declaration consistent with its implementation.
define EELINUX_EXPECT_TTY_PROTOTYPE
	printf '%s  %s\n' e06daf6bcde223f88b42b39e094e548e3226ffdb8b97f391359ce4570b351383 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/expect-5.45.4-tty-prototype.patch" | sha256sum -c -
	printf '%s  %s\n' 243dce2351ea9d7a3255aa4797f01f8b887d2f2e2a51305886e6a6da9f08452b \
		"$(@D)/exp_tty.h" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/expect-5.45.4-tty-prototype.patch"
	printf '%s  %s\n' 1bc7729cb996d1f6202d8665d8061d346d21e0d4f89515e4e8556d26003590ea \
		"$(@D)/exp_tty.h" | sha256sum -c -
endef
EXPECT_POST_PATCH_HOOKS += EELINUX_EXPECT_TTY_PROTOTYPE

define EELINUX_EXPECT_POSIX_PROTOTYPES
	printf '%s  %s\n' a2f220cf36c6633978137e6243818fe1e8fdd5e110a636873c2400204ae7d961 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/expect-5.45.4-posix-prototypes.patch" | sha256sum -c -
	printf '%s  %s\n' 5c6462e6333ccf13c1da71ccaee51ca95a9a66a7d6409c76648c853f2a71fa73 \
		"$(@D)/exp_clib.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/expect-5.45.4-posix-prototypes.patch"
	printf '%s  %s\n' d2fa31b33dc8bc1d361a2d79dde94976dbb586986b927d79fc51ed2258764b1f \
		"$(@D)/exp_clib.c" | sha256sum -c -
endef
EXPECT_POST_PATCH_HOOKS += EELINUX_EXPECT_POSIX_PROTOTYPES

# Keep Expect's channel callbacks in Tcl's documented version-two fields.
define EELINUX_EXPECT_CHANNEL_API
	printf '%s  %s\n' 0e876bfd63d8d2ca347c2a076c2056aa11ad6ff647487dd30ac690815f4b7305 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/expect-5.45.4-channel-api.patch" | sha256sum -c -
	printf '%s  %s\n' 64e00fbad6cfe9d4ab2908e0bc5049b67addf8d8d9239f3a8cf3536c976b8279 "$(@D)/exp_int.h" \
		8f195268a3b50c59885c254f3abb403180e21c6da851ef67d16da84e42f6655d "$(@D)/pty_termios.c" \
		c22d5e5717d3504ec9116f3e5df6ed048830cae79355e5754a148a7d620b8a5c "$(@D)/exp_chan.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/expect-5.45.4-channel-api.patch"
	printf '%s  %s\n' 35e5ef3580e3745ed2c4fe18085ed4e0db85b0490f32f7870e426427076cb1fc "$(@D)/exp_int.h" \
		702c90fd48c7219c25d6d218232d5c01e0a3d4bb8fc5ffb921d9013709edb376 "$(@D)/pty_termios.c" \
		74c85e6d9ffa87fa2513bf34e9b1fcadf27288eb4fb3d23e01f1c1149af0f530 "$(@D)/exp_chan.c" | sha256sum -c -
endef
EXPECT_POST_PATCH_HOOKS += EELINUX_EXPECT_CHANNEL_API

# Bash 5.2 uses pre-C23 declarations; ed 1.20 declares its own bool type.
# GCC 15 changed its C default;
# select the dialect supported by this release without changing C++ flags.
# https://gcc.gnu.org/gcc-15/porting_to.html#c23
ifeq ($(BR2_TOOLCHAIN_GCC_AT_LEAST_15),y)
BASH_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
GDBM_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
# iftop 1.0pre4 uses legacy unspecified-argument declarations.
IFTOP_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
# iperf 3.18 uses empty callback parameter lists with pre-C23 semantics.
IPERF3_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -D_GNU_SOURCE -std=gnu17"
# GNU time 1.9 declares its signal callback using pre-C23 syntax.
ifeq ($(TIME_VERSION),1.9)
TIME_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
endif
# ngrep 1.47 uses unspecified callback arguments, predating C23.
NGREP_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
# NTP's older declaration probes predate glibc's C23 memchr macro.
NTP_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
# Expect 5.45.4's exit callback definitions use unspecified argument lists.
EXPECT_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
define ED_CONFIGURE_CMDS
	(cd $(@D); \
		$(TARGET_MAKE_ENV) ./configure --prefix=/usr \
		$(TARGET_CONFIGURE_OPTS) CFLAGS="$(TARGET_CFLAGS) -std=gnu17")
endef
endif

# Preserve ncurses' generated public ABI. Nano's intended pointer truth tests
# must be explicit when its bool macro resolves to NCURSES_BOOL (an integer).
define EELINUX_NANO_EXPLICIT_BOOL
	printf '%s  %s\n' abefb85887418089dbde6b7227d101bffbf7f0b1357347d410811757f2e2f17b \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/nano-8.2-explicit-bool.patch" | sha256sum -c -
	printf '%s  %s\n' f42a1c129af26a27f44441b96284c7d2f1c20f0572c52564ee1bb9c969db7732 \
		"$(@D)/src/move.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/nano-8.2-explicit-bool.patch"
	printf '%s  %s\n' 77c7ecc083a7b30a997255cdd833ac8136160a08bf2d419890649c166901b6a7 \
		"$(@D)/src/move.c" | sha256sum -c -
endef
NANO_POST_PATCH_HOOKS += EELINUX_NANO_EXPLICIT_BOOL

# Keep Buildroot's internal "custom" version for local-source stamp paths.
# The actual upstream version is passed separately to SysVinit's Makefile.
# Changing SYSVINIT_VERSION after generic-package creates rules breaks its
# bookkeeping paths: rules use sysvinit-custom while callbacks use 3.13.
ifeq ($(EELINUX_USE_VENDORED_SYSVINIT),y)
SYSVINIT_SITE = $(BR2_EXTERNAL_EELINUX_PATH)/sysvinit
SYSVINIT_SITE_METHOD = local
SYSVINIT_MAKE_OPTS += ROOT=$(STAGING_DIR) SYSROOT=$(STAGING_DIR) VERSION=3.13
else
SYSVINIT_MAKE_OPTS += ROOT=$(STAGING_DIR) SYSROOT=$(STAGING_DIR) VERSION=$(SYSVINIT_VERSION)
endif

# Shadow 4.20 uses the system crypt implementation and no longer offers the
# old bundled SHA/attr/cracklib/account-tools configure switches. PAM retains
# password policy, and Buildroot's permission table retains installed modes.
# This SysVinit system has no logind service; use the normal PAM session path.
ifeq ($(SHADOW_VERSION),4.20.3)
SHADOW_CONF_OPTS := $(filter-out --enable-account-tools-setuid --disable-account-tools-setuid \
	--with-sha-crypt --without-sha-crypt --with-attr --without-attr \
	--with-libcrack --without-libcrack,$(SHADOW_CONF_OPTS)) --disable-logind
endif

# The standard recipe installs only four SysVinit programs. Install the real
# runlevel controls and its additional diagnostics from the same compiled suite.
define EELINUX_SYSVINIT_EXTRA_TOOLS
	for program in runlevel fstab-decode bootlogd logsave; do \
		$(INSTALL) -D -m 0755 $(@D)/src/$$program \
			$(TARGET_DIR)/sbin/$$program || exit 1; \
	done
	for program in last mesg readbootlog utmpdump wall; do \
		$(INSTALL) -D -m 0755 $(@D)/src/$$program \
			$(TARGET_DIR)/usr/bin/$$program || exit 1; \
	done
	ln -sf init $(TARGET_DIR)/sbin/telinit
	ln -sf last $(TARGET_DIR)/usr/bin/lastb
endef
SYSVINIT_POST_INSTALL_TARGET_HOOKS += EELINUX_SYSVINIT_EXTRA_TOOLS

# Buildroot normally builds only libfl on the target. The development profile
# needs the flex executable too, with target GNU m4 selected in the profile.
ifeq ($(BR2_PACKAGE_EELINUX_GCC),y)
FLEX_CONF_OPTS += --enable-program
FLEX_DEPENDENCIES += eelinux-m4
endif

# Keep the core X11 extensions used by ordinary desktop clients. The desktop
# builder installs the native PS2 GS DDX after the core server is staged.
XSERVER_XORG_SERVER_CONF_OPTS += --disable-dri2 --disable-dri3 --disable-glamor \
	--disable-config-udev --disable-config-dbus --disable-composite
XAPP_TWM_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"
XLIB_LIBXT_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -std=gnu17"

# Shaped-window image replies must clear hidden pixels through driver-owned
# storage; a fake CPU-backed pixmap bypasses hardware-only DDX allocation.
define EELINUX_XORG_ACCELERATED_IMAGE_CENSOR
	printf '%s  %s\n' 942dbb51ea65dac34ab453897403ab65452d9b782bb18efabe936e839175af4b \
		"$(BR2_EXTERNAL_EELINUX_PATH)/package/x11r7/xserver_xorg-server/0002-censor-images-with-driver-owned-pixmaps.patch" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/package/x11r7/xserver_xorg-server/0002-censor-images-with-driver-owned-pixmaps.patch"
endef
XSERVER_XORG_SERVER_POST_PATCH_HOOKS += EELINUX_XORG_ACCELERATED_IMAGE_CENSOR

# Bound uname's nodename read without disabling GCC's array-bounds diagnostics.
define PS2_XTRANS_BOUNDED_HOSTNAME
	printf '%s  %s\n' 9d2826b9afb6816ee71f788989ac855965c2c0df151e32a1a7bda3d63e531e8a \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/xtrans-bounded-hostname.patch" | sha256sum -c -
	printf '%s  %s\n' 50bdd12137b24997bc03c9ebcf7602f3f3c40a62a74f5e9d46ce15ee74977f80 \
		"$(@D)/Xtrans.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/xtrans-bounded-hostname.patch"
endef
XLIB_XTRANS_POST_PATCH_HOOKS += PS2_XTRANS_BOUNDED_HOSTNAME

# The selected ncurses ABI uses a bool compatibility macro. Keep it scoped to
# the header declarations instead of changing C++ templates/program variables.
define EELINUX_NETHOGS_CXX_BOOL
	printf '%s  %s\n' 8ac40001f053f2ac29b936df1cf1fbcc916af4780765dfbcb3be00bb132f3296 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/nethogs-0.8.8-cxx-bool.patch" | sha256sum -c -
	printf '%s  %s\n' c468973160ac3acdb3e6ea35c9c3c0bc55f9fe305c084ab9291f121708ae7cc4 \
		"$(@D)/src/process.cpp" | sha256sum -c -
	printf '%s  %s\n' 88cc4e8d0091a587472fae1ff8e87d1e73159023d8643e28e1d526fb126d93b2 \
		"$(@D)/src/cui.cpp" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/nethogs-0.8.8-cxx-bool.patch"
	printf '%s  %s\n' 632e4dde598bfa384cca4f884f92bc341e3d8d21abd95099de6c1b995b32dd6f \
		"$(@D)/src/process.cpp" | sha256sum -c -
	printf '%s  %s\n' 2f6a21940e773931507aeb69a75b84a8e2939fa7bb7dad6f438686524e48bed8 \
		"$(@D)/src/cui.cpp" | sha256sum -c -
endef
ifeq ($(NETHOGS_VERSION),0.8.8)
NETHOGS_POST_PATCH_HOOKS += EELINUX_NETHOGS_CXX_BOOL
endif

# Latest release hooks are independently source- and result-checked.
define EELINUX_NMAP_LATEST_SOURCE_FIX
	printf '%s  %s\n' b403ecb81e048d4cee5abd6ae5b8eebf498aa622c1e95c6bbeeeb7421d9623f1 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/nmap-7.993-lua-cross-tools.patch" | sha256sum -c -
	printf '%s  %s\n' aa603c7cffcc6785683322e86edb942e7e3b0d694a88623bf91ad85241dacfad "$(@D)/Makefile.in" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/nmap-7.993-lua-cross-tools.patch"
	printf '%s  %s\n' ff463d784da13fa29c65aa6d426e1b702f3fe8a1c8a26251b21c4795e32b1acb "$(@D)/Makefile.in" | sha256sum -c -
endef
ifeq ($(NMAP_VERSION),7.993)
NMAP_POST_PATCH_HOOKS += EELINUX_NMAP_LATEST_SOURCE_FIX
endif

define EELINUX_NETHOGS_LATEST_SOURCE_FIX
	printf '%s  %s\n' 0f3ec3c0b765c9244740e7e3acfa19db1c7f8ebbe369b84102cf4ab26c0dcf0a \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/nethogs-0.9.0-cxx-bool.patch" | sha256sum -c -
	printf '%s  %s\n' c468973160ac3acdb3e6ea35c9c3c0bc55f9fe305c084ab9291f121708ae7cc4 "$(@D)/src/process.cpp" | sha256sum -c -
	printf '%s  %s\n' aa96c9869871e1e19239a511c957d55146e3e5b78632ebe97a8a048491a364d9 "$(@D)/src/cui.cpp" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/nethogs-0.9.0-cxx-bool.patch"
	printf '%s  %s\n' 632e4dde598bfa384cca4f884f92bc341e3d8d21abd95099de6c1b995b32dd6f "$(@D)/src/process.cpp" | sha256sum -c -
	printf '%s  %s\n' 953dc600a54ff1f2fb0c10cc918942c418bd153332df920dc1deb1de6eb965f9 "$(@D)/src/cui.cpp" | sha256sum -c -
endef
ifeq ($(NETHOGS_VERSION),0.9.0)
NETHOGS_POST_PATCH_HOOKS += EELINUX_NETHOGS_LATEST_SOURCE_FIX
endif

# The 2.5 Waf probe now uses Fortify3. Keep its default hardening intact while
# retaining Buildroot's explicit opt-out for toolchains without SSP support.
define EELINUX_LIBTALLOC_250_STACK_PROTECTOR
	printf '%s  %s\n' 35504549a4120470122ef94e19b577960511e47efae2489956fa77bebfc56529 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/talloc-2.5.0-disable-stack-protector.patch" | sha256sum -c -
	printf '%s  %s\n' 2bba58806c25b0524e1964a27172c4eab3936c5d6f36e915e3d5f5983e4229bc \
		"$(@D)/buildtools/wafsamba/samba_autoconf.py" \
		83f903ab361a9303b6f180577b47a601ce9ba913ca025091eead3f7f3f05dd8b \
		"$(@D)/buildtools/wafsamba/wscript" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/talloc-2.5.0-disable-stack-protector.patch"
	printf '%s  %s\n' 8416e776543b97cf43245c25fb180de89169c19164a804a1de92146d9704ad7b \
		"$(@D)/buildtools/wafsamba/samba_autoconf.py" \
		acccb6d5ac5686a9be4f86d1a6c72a94ea546b586fd9ea893d68d425e4be1c19 \
		"$(@D)/buildtools/wafsamba/wscript" | sha256sum -c -
endef
ifeq ($(LIBTALLOC_VERSION),2.5.0)
LIBTALLOC_POST_PATCH_HOOKS += EELINUX_LIBTALLOC_250_STACK_PROTECTOR
endif


# Preserve the Curses ABI while restoring C++ bool in nload private code.
define EELINUX_NLOAD_CURSES_BOOL_SCOPE
	printf '%s  %s\n' f133ca110bc2ef712308832a5f7cdc0a8b3a9523a415eba8488169846047d05f "$(BR2_EXTERNAL_EELINUX_PATH)/patches/nload-0.7.4-curses-bool-scope.patch" | sha256sum -c -
	printf '%s  %s\n' e51798f0dfc5b19d9bdbda2004d1ae8cd36c5bf8c3444a18472ec276574ea1c7 "$(@D)/src/window.h" | sha256sum -c -
	printf '%s  %s\n' 4c60fb500b096a3a8d2cb6b8781037f56e58ff8c3265d70e885f7e40f1b3e80a "$(@D)/src/main.cpp" | sha256sum -c -
	printf '%s  %s\n' f70995c8758d7b82af4f92feb1308b74dd632ed7f3079e1ae65f97dfc92e5139 "$(@D)/src/form_field.h" | sha256sum -c -
	printf '%s  %s\n' f420a9be9b3bc8c433fbd592c83d0eeef3298306b7328bcf5ed4ff0013ccb48f "$(@D)/src/screen.cpp" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 -i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/nload-0.7.4-curses-bool-scope.patch"
	printf '%s  %s\n' e50e47bbf654c15279606e25c7337f3c359e632ab463f5a3a6f49f3527b606ff "$(@D)/src/window.h" | sha256sum -c -
	printf '%s  %s\n' b158299b83baa6a6e57e04cd1148bb6780a1608e31a90b5036ac1b834d405274 "$(@D)/src/main.cpp" | sha256sum -c -
	printf '%s  %s\n' 162b44123ce459f448f8743809bb7446d5fcc4885baaac24c95f0129cf5f836b "$(@D)/src/form_field.h" | sha256sum -c -
	printf '%s  %s\n' a5115a6ed9ee8498fddf27341305bd4f3ab38424ffa160fd4d00020f1404d9ea "$(@D)/src/screen.cpp" | sha256sum -c -
endef
ifeq ($(NLOAD_VERSION),0.7.4)
NLOAD_POST_PATCH_HOOKS += EELINUX_NLOAD_CURSES_BOOL_SCOPE
endif

# Use upstream's signal-safe diagnostic fallback when native atomics are absent.
define EELINUX_LZ4_111_MMAP_DIAGNOSTIC_ATOMICS
	printf '%s  %s\n' 3da1a9056fe3b1f946a123fe6270ad7b1154f83eb04e2fe274f578d77b15d2a2 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/lz4-1.11.0-r5900-mmap-diagnostic.patch" | sha256sum -c -
	printf '%s  %s\n' 6e3dc69080a0db74440a33cfe0d704d8d83d810daadc7d65c5878fa6d8872797 \
		"$(@D)/programs/lz4io.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/lz4-1.11.0-r5900-mmap-diagnostic.patch"
	printf '%s  %s\n' c4e368bbab072be19262c52028a432e7d0f454e0eaef0ac0bafc5964faec132d \
		"$(@D)/programs/lz4io.c" | sha256sum -c -
endef
ifeq ($(LZ4_VERSION),1.11.0)
LZ4_POST_PATCH_HOOKS += EELINUX_LZ4_111_MMAP_DIAGNOSTIC_ATOMICS
endif

# OpenSSL 4 replaces the former Perl c_rehash program with openssl rehash.
# Keep the pinned certificate data, sorted bundle and runtime trust directory.
define CA_CERTIFICATES_GEN_BUNDLE
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/ssl/certs
	rm -f $(TARGET_DIR)/etc/ssl/certs/*
	cd $(TARGET_DIR) ; \
	for i in `find usr/share/ca-certificates -name "*.crt" | LC_COLLATE=C sort` ; do \
		ln -sf ../../../$$i etc/ssl/certs/`basename $${i} .crt`.pem ; \
		cat $$i ; \
	done >$(BUILD_DIR)/ca-certificates.crt
	$(HOST_DIR)/bin/openssl rehash $(TARGET_DIR)/etc/ssl/certs
	$(INSTALL) -D -m 0644 $(BUILD_DIR)/ca-certificates.crt $(TARGET_DIR)/etc/ssl/certs/ca-certificates.crt
endef

# Use LZ4's public enum and normalize Zstd's explicit --check value (2) to
# LZ4's enabled value (1), retaining the requested frame content checksum.
define EELINUX_ZSTD_LZ4_PUBLIC_ENUM
	printf '%s  %s\n' 58a47be2ebeedc6101a0dc752e74853b2a36fd3fed7f6008bd1100592fb8aa1b \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/zstd-1.5.7-lz4-public-enum.patch" | sha256sum -c -
	printf '%s  %s\n' 612ab03945989514c8f4285623451902ff2ebc8453b82970b4ab4efca3d8246a "$(@D)/programs/fileio.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/zstd-1.5.7-lz4-public-enum.patch"
	printf '%s  %s\n' fe6b17bec5e2e1a973e9ad1625246bdd1181023193c27df7decc1ca6cda1f510 "$(@D)/programs/fileio.c" | sha256sum -c -
endef
ifeq ($(ZSTD_VERSION),1.5.7)
ZSTD_POST_PATCH_HOOKS += EELINUX_ZSTD_LZ4_PUBLIC_ENUM
endif

# Skarnet's install targets compare destination mtimes with archive mtimes.
# Refresh the provider-owned headers explicitly after installation so an
# incremental version upgrade cannot retain newer-dated, older-content files.
define EELINUX_SKALIBS_REFRESH_HEADERS
	$(INSTALL) -d -m 0755 $(1)/include/skalibs
	$(INSTALL) -m 0644 $(@D)/src/include/skalibs/*.h $(1)/include/skalibs/
endef
define EELINUX_SKALIBS_REFRESH_STAGING_HEADERS
	$(call EELINUX_SKALIBS_REFRESH_HEADERS,$(STAGING_DIR))
endef
define EELINUX_SKALIBS_REFRESH_TARGET_HEADERS
	$(call EELINUX_SKALIBS_REFRESH_HEADERS,$(TARGET_DIR))
endef
define EELINUX_HOST_SKALIBS_REFRESH_HEADERS
	$(call EELINUX_SKALIBS_REFRESH_HEADERS,$(HOST_DIR))
endef
SKALIBS_POST_INSTALL_STAGING_HOOKS += EELINUX_SKALIBS_REFRESH_STAGING_HEADERS
SKALIBS_POST_INSTALL_TARGET_HOOKS += EELINUX_SKALIBS_REFRESH_TARGET_HEADERS
HOST_SKALIBS_POST_INSTALL_HOOKS += EELINUX_HOST_SKALIBS_REFRESH_HEADERS

# OpenSSL 4 exposes borrowed certificate fields through const pointers.
define EELINUX_LFTP_493_CONST_CERTIFICATE_FIELDS
	printf '%s  %s\n' 216e3ef95b71fceba0e7cf1df689e54b01ce4ece1fa0c86641b6d93ae464b09a \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/lftp-4.9.3-openssl4-const-certificates.patch" | sha256sum -c -
	printf '%s  %s\n' 85e6ba38eb7c3e60a88bed9f9ac6955b66d8f2eb8324b1917367286450f8e36e \
		"$(@D)/src/lftp_ssl.cc" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/lftp-4.9.3-openssl4-const-certificates.patch"
	printf '%s  %s\n' 269239abdbfc0acd5ae54b1404e7bfea6767229916690828d390c61645e1f6b2 \
		"$(@D)/src/lftp_ssl.cc" | sha256sum -c -
endef
ifeq ($(LFTP_VERSION),4.9.3)
LFTP_POST_PATCH_HOOKS += EELINUX_LFTP_493_CONST_CERTIFICATE_FIELDS
endif

# Stop the first application operation when certificate verification failed.
define EELINUX_LFTP_493_STOP_CERTIFICATE_ERROR
	printf '%s  %s\n' 018a268f1bd6fdf5bc7d17045a4e94f9c82a3509bc880868f184c78584e48155 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/lftp-4.9.3-stop-on-certificate-error.patch" | sha256sum -c -
	printf '%s  %s\n' 269239abdbfc0acd5ae54b1404e7bfea6767229916690828d390c61645e1f6b2 \
		"$(@D)/src/lftp_ssl.cc" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/lftp-4.9.3-stop-on-certificate-error.patch"
	printf '%s  %s\n' f8254430b39ba33b81f03b6f4eddbb1add112f26fced0570cdfcce9f13a369c5 \
		"$(@D)/src/lftp_ssl.cc" | sha256sum -c -
endef
ifeq ($(LFTP_VERSION),4.9.3)
LFTP_POST_PATCH_HOOKS += EELINUX_LFTP_493_STOP_CERTIFICATE_ERROR
endif

# These DNS interfaces expose or statically link the maintained OpenSSL 3 ABI.
# Put its include/library search paths first and keep native SDK metadata free
# of the cross-build sysroot. Runtime objects retain their normal SONAMEs.
define EELINUX_LDNS_OPENSSL3_PKGCONFIG
	for pc_root in $(STAGING_DIR) $(TARGET_DIR); do \
		pc="$$pc_root/usr/lib/pkgconfig/ldns.pc"; \
		if test -f "$$pc"; then \
			$(SED) 's|$(STAGING_DIR)||g' \
				-e 's|^Requires:.*|Requires: openssl3|' \
				-e 's|^Libs:.*|Libs: -L$${prefix}/lib/openssl3 -L$${libdir} -lldns|' \
				-e 's|^Cflags:.*|Cflags: -I$${prefix}/include/openssl3 -I$${includedir}|' "$$pc"; \
		fi; \
	done
endef
ifeq ($(LIBLDNS_VERSION),1.9.2)
ifneq ($(findstring /usr/include/openssl3,$(LIBLDNS_CONF_OPTS)),)
LIBLDNS_POST_INSTALL_STAGING_HOOKS += EELINUX_LDNS_OPENSSL3_PKGCONFIG
LIBLDNS_POST_INSTALL_TARGET_HOOKS += EELINUX_LDNS_OPENSSL3_PKGCONFIG
endif
endif

define EELINUX_UNBOUND_OPENSSL3_PKGCONFIG
	for pc_root in $(STAGING_DIR) $(TARGET_DIR); do \
		pc="$$pc_root/usr/lib/pkgconfig/libunbound.pc"; \
		if test -f "$$pc"; then \
			$(SED) 's|$(STAGING_DIR)||g' \
				-e '/^Requires[.:]/s/\<libssl\>//g' \
				-e '/^Requires[.:]/s/\<libcrypto\>//g' \
				-e '/^Requires.private:/s/\<openssl3\>//g' \
				-e '/^Requires.private:/s/$$/ openssl3/' \
				-e 's|^Libs:.*|Libs: -L$${prefix}/lib/openssl3 -L$${libdir} -lunbound|' \
				-e 's|^Cflags:.*|Cflags: -I$${prefix}/include/openssl3 -I$${includedir}|' "$$pc"; \
		fi; \
	done
endef
ifeq ($(UNBOUND_VERSION),1.26.2)
ifneq ($(findstring /usr/include/openssl3,$(UNBOUND_CONF_OPTS)),)
UNBOUND_POST_INSTALL_STAGING_HOOKS += EELINUX_UNBOUND_OPENSSL3_PKGCONFIG
UNBOUND_POST_INSTALL_TARGET_HOOKS += EELINUX_UNBOUND_OPENSSL3_PKGCONFIG
endif
endif

# The digest implementation units already provide the external wrappers.
define EELINUX_LFTP_493_OPENSSL_HASH_INLINE
	printf '%s  %s\n' e35b4bdeccfce0e7e7dd44a30bcccb2689a556937612fd4949b7fa87300347af \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/lftp-4.9.3-openssl-hash-single-definition.patch" | sha256sum -c -
	printf '%s  %s\n' 893ed41268af0a15fed3915aa9d3c87a01b04a8540916bdff4250f96bb65c542 "$(@D)/lib/md5-stream.c" \
		87c5ad8d8395f1cf409404fad29fac7224dea2804f88eac1d4d5edc932a74c5c "$(@D)/lib/sha1-stream.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/lftp-4.9.3-openssl-hash-single-definition.patch"
	printf '%s  %s\n' 27e94e8fbcde71357aefbaf53b0a5f07bf3b9142292eb2f64c8929813f1f0c83 "$(@D)/lib/md5-stream.c" \
		492d2d500c38ec35d42267bb7cfc5e474f9930088df91bae3b15c33be06d3c4e "$(@D)/lib/sha1-stream.c" | sha256sum -c -
endef
ifeq ($(LFTP_VERSION),4.9.3)
LFTP_POST_PATCH_HOOKS += EELINUX_LFTP_493_OPENSSL_HASH_INLINE
endif

# Keep the fallback declaration valid when libc provides a C23 memchr macro.
define EELINUX_NTP_428P18_MACRO_SAFE_MEMCHR
	printf '%s  %s\n' c8d94120d7733524657606a2b8df198dd5471530aab777dbd07ef2a3dd0e42a7 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/ntp-4.2.8p18-macro-safe-memchr.patch" | sha256sum -c -
	printf '%s  %s\n' 731e3a2463aaf148fc3c7f3dd768ebe81b456eda6333a0081603e36a077ca201 \
		"$(@D)/include/l_stdlib.h" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/ntp-4.2.8p18-macro-safe-memchr.patch"
	printf '%s  %s\n' 4a2239492565e74cff35ce85b6add658a2b23aabdae99fe498bdf8c64b8beefc \
		"$(@D)/include/l_stdlib.h" | sha256sum -c -
endef
ifeq ($(NTP_VERSION),4.2.8p18)
NTP_POST_PATCH_HOOKS += EELINUX_NTP_428P18_MACRO_SAFE_MEMCHR
endif

# Libtool must not turn cross-build staging directories into client runpaths.
define EELINUX_NTP_428P18_CROSS_RUNPATHS
	printf '%s  %s\n' 0cea6d0ae74fd62e80e62ecfd83e8ffe12c45fee87448db34431cac8c0c39640 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/ntp-4.2.8p18-sntp-cross-runpaths.patch" | sha256sum -c -
	printf '%s  %s\n' 78750be6647b14579f75dd268810d0289464d35677d2b4c0455c37ac414987cf \
		"$(@D)/configure.ac" | sha256sum -c -
	printf '%s  %s\n' 541f2b1c4184aec7366e033f8f003712c7d5463bc1d693bb0f0e64ba11db8846 \
		"$(@D)/sntp/configure.ac" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/ntp-4.2.8p18-sntp-cross-runpaths.patch"
	printf '%s  %s\n' 0a17bda2dd8c81e939b349b8ec640b2a88bffbb24913de6a63ffc84d383f9b15 \
		"$(@D)/configure.ac" | sha256sum -c -
	printf '%s  %s\n' 40c99760c09b4040962ba1a0c921837addbc5c339f2da4eb8e8a36208e296ed9 \
		"$(@D)/sntp/configure.ac" | sha256sum -c -
endef
ifeq ($(NTP_VERSION),4.2.8p18)
NTP_POST_PATCH_HOOKS += EELINUX_NTP_428P18_CROSS_RUNPATHS
endif

# Disabled NSCD uses the same service-name argument type as the real API.
define EELINUX_SHADOW_4203_NSCD_STUB_SIGNATURE
	printf '%s  %s\n' 83022804dc0146bf37ce641d4052aacb5740aa7c998b6adbccc50b3f395db99b \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/shadow-4.20.3-nscd-stub-signature.patch" | sha256sum -c -
	printf '%s  %s\n' 682a002f2821c00f345df4be5b27425fcd62b280c619daf3418336cf4ad86aad \
		"$(@D)/lib/nscd.h" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/shadow-4.20.3-nscd-stub-signature.patch"
	printf '%s  %s\n' ae1b35104f976431ef97175f4c4904d91df0b42563acdfd707d3c6d06a71536a \
		"$(@D)/lib/nscd.h" | sha256sum -c -
endef
ifeq ($(SHADOW_VERSION),4.20.3)
SHADOW_POST_PATCH_HOOKS += EELINUX_SHADOW_4203_NSCD_STUB_SIGNATURE
endif

# OpenSSL 4 makes certificate string storage opaque and borrowed fields const.
define EELINUX_SOCAT_1813_CERTIFICATE_ACCESSORS
	printf '%s  %s\n' 6b07f0646be13ec3dc30ad204acafc4b51ac96bc96973ec327b6b38c2c84ab5b \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/socat-1.8.1.3-openssl4-certificate-accessors.patch" | sha256sum -c -
	printf '%s  %s\n' 0ff0f6af9a47232cc77a93f8aa87bae860ef32a335a105f909ac75552cccd355 \
		"$(@D)/xio-openssl.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/socat-1.8.1.3-openssl4-certificate-accessors.patch"
	printf '%s  %s\n' 41d20cc98764d211b6500cb74a58bfbccb497f777828eb3df8cd715ff65e73ce \
		"$(@D)/xio-openssl.c" | sha256sum -c -
endef
ifeq ($(SOCAT_VERSION),1.8.1.3)
SOCAT_POST_PATCH_HOOKS += EELINUX_SOCAT_1813_CERTIFICATE_ACCESSORS
endif

# AC_TYPE_OFF_T already owns the fallback; keep the libc large-file typedef.
define EELINUX_TFTPD_72_AUTOCONF_OFF_T
	printf '%s  %s\n' 58031ff9f3d7524bc632a82d7ae4f324b5a0909bf72383d0d2834b92e23a330f \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/tftp-hpa-7.2-autoconf-off-t.patch" | sha256sum -c -
	printf '%s  %s\n' ee154053ff2851d8192465a75d1cea42c297d2bc2520c94829a43a0837ec9927 \
		"$(@D)/config.h" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/tftp-hpa-7.2-autoconf-off-t.patch"
	printf '%s  %s\n' 73b5862041f11c5201a776b32263b14274b0688971465e37519faef2cf9a01ab \
		"$(@D)/config.h" | sha256sum -c -
endef
ifeq ($(TFTPD_VERSION),7.2)
TFTPD_POST_PATCH_HOOKS += EELINUX_TFTPD_72_AUTOCONF_OFF_T
endif

# Match the timeout API's writable uintmax_t pointer on 32-bit targets.
define EELINUX_TFTPD_72_TIMEOUT_WIDTH
	printf '%s  %s\n' 9d3a8a636c8048ea4c4bd79eb4897f2774cfaf06b8a3a163f59d0e86d913ed75 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/tftp-hpa-7.2-timeout-width.patch" | sha256sum -c -
	printf '%s  %s\n' 02fe13769305b77ec229e12b5b072a737b0de3ed67f837fc954c6b77551e38ad \
		"$(@D)/tftp/tftp.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/tftp-hpa-7.2-timeout-width.patch"
	printf '%s  %s\n' 00278d7fdc81078a1845d105f0291f1dcff10c96ea2ddc671a853db0fefc68d4 \
		"$(@D)/tftp/tftp.c" | sha256sum -c -
endef
ifeq ($(TFTPD_VERSION),7.2)
TFTPD_POST_PATCH_HOOKS += EELINUX_TFTPD_72_TIMEOUT_WIDTH
endif

# Online ext4 resizing uses mountpoint ioctls, so its raw metadata fd is read-only.
# Keep offline read/write + exclusive protection and the kernel mounted-write ban.
define EELINUX_E2FSPROGS_1474_MOUNTED_READONLY_METADATA
	printf '%s  %s\n' 32da6ee0ac7e37248c72072604b6d3aafaa838ad629671b05b0abd6a79ac6dcd \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/e2fsprogs-1.47.4-mounted-readonly-metadata.patch" | sha256sum -c -
	printf '%s  %s\n' 02d01ea8b94f1d4025e2c87ef2314e646ae28716308f773c946e40e716be8bbb \
		"$(@D)/resize/main.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/e2fsprogs-1.47.4-mounted-readonly-metadata.patch"
	printf '%s  %s\n' 3454a2abf22b8708b46a5735513c33b0df2ee5b64a446d85d93e1f8a44e1231a \
		"$(@D)/resize/main.c" | sha256sum -c -
endef
ifeq ($(E2FSPROGS_VERSION),1.47.4)
E2FSPROGS_POST_PATCH_HOOKS += EELINUX_E2FSPROGS_1474_MOUNTED_READONLY_METADATA
endif

# libacl 2.4.0 adds acl_get_file_at, acl_set_file_at and acl_delete_def_file_at
# with types that differ from tar 1.35's private static wrappers of the same
# names. Rename tar's wrappers; ACL archiving behavior is unchanged.
define EELINUX_TAR_135_ACL_24_WRAPPER_NAMES
	printf '%s  %s\n' 0f911ef76c37a9c81467d23f2a09159cef083f98d928abdbf4aede461c1ae8e6 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/tar-1.35-acl-2.4-wrapper-names.patch" | sha256sum -c -
	printf '%s  %s\n' 8598a61c46ece2530cde835d3bf8ae99f5a7999207da593ccbbccdb767d521d6 "$(@D)/src/xattrs.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/tar-1.35-acl-2.4-wrapper-names.patch"
	printf '%s  %s\n' a77499c4369e86eb4e9e4dc089bf8ec0d1558e8a862d05e9962480e381b6f4af "$(@D)/src/xattrs.c" | sha256sum -c -
endef
ifeq ($(TAR_VERSION),1.35)
TAR_POST_PATCH_HOOKS += EELINUX_TAR_135_ACL_24_WRAPPER_NAMES
endif

# tclZipfs.c needs minizip's crypt.h from Tcl's bundled zlib, but HOST_DIR/include
# (searched first) also holds host-libxcrypt's unrelated <crypt.h>.
define EELINUX_HOST_TCL_910_MINIZIP_CRYPT_HEADER
	printf '%s  %s\n' 1aa87864ec88e641169dbeb09cff800197d2a4cbcd0701d2170a217113899dd7 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/tcl-9.1.0-minizip-crypt-header.patch" | sha256sum -c -
	printf '%s  %s\n' 082fe1b161f35b113fc7e7d2db9d525120e7077aed2d142aa6958cb05d741e08 "$(@D)/generic/tclZipfs.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/tcl-9.1.0-minizip-crypt-header.patch"
	printf '%s  %s\n' 14984857564497686e5e486350bbfc22455a92e3ff040ee4873df3b193e68127 "$(@D)/generic/tclZipfs.c" | sha256sum -c -
endef
ifeq ($(TCL_VERSION),9.1.0)
HOST_TCL_POST_PATCH_HOOKS += EELINUX_HOST_TCL_910_MINIZIP_CRYPT_HEADER
TCL_POST_PATCH_HOOKS += EELINUX_HOST_TCL_910_MINIZIP_CRYPT_HEADER
endif

# OpenSSL 4 removed SSLv3_client_method without defining OPENSSL_NO_SSL3_METHOD.
# --secure-protocol=SSLv3 then fails cleanly, as on SSLv3-less OpenSSL 3 builds.
define EELINUX_WGET_1250_OPENSSL4_SSLV3
	printf '%s  %s\n' 5c9ccd590198eb7a7d2bebaa97d7de704d9c6909114f7de145fcc7e048784c58 \
		"$(BR2_EXTERNAL_EELINUX_PATH)/patches/wget-1.25.0-openssl4-sslv3.patch" | sha256sum -c -
	printf '%s  %s\n' e9a433bae1863961c2d098ff3658397cbc983d78fe7fe279f7fb3135a21ad3da "$(@D)/src/openssl.c" | sha256sum -c -
	cd $(@D) && patch --batch --forward --fuzz=0 -p1 \
		-i "$(BR2_EXTERNAL_EELINUX_PATH)/patches/wget-1.25.0-openssl4-sslv3.patch"
	printf '%s  %s\n' cd89fa02616bc3fcae6b97306da8b5379d6bc99db32ef3ec7bf5d0dcedb8da36 "$(@D)/src/openssl.c" | sha256sum -c -
endef
ifeq ($(WGET_VERSION),1.25.0)
WGET_POST_PATCH_HOOKS += EELINUX_WGET_1250_OPENSSL4_SSLV3
endif
