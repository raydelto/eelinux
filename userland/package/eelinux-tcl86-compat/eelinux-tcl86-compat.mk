################################################################################
# Latest maintained Tcl 8.6 line, private compatibility SDK for stable Expect.
################################################################################
EELINUX_TCL86_COMPAT_VERSION = 8.6.18
EELINUX_TCL86_COMPAT_SOURCE = tcl$(EELINUX_TCL86_COMPAT_VERSION)-src.tar.gz
EELINUX_TCL86_COMPAT_SITE = https://downloads.sourceforge.net/project/tcl/Tcl/$(EELINUX_TCL86_COMPAT_VERSION)
EELINUX_TCL86_COMPAT_LICENSE = TCL
EELINUX_TCL86_COMPAT_LICENSE_FILES = license.terms
EELINUX_TCL86_COMPAT_SUBDIR = unix
EELINUX_TCL86_COMPAT_INSTALL_STAGING = YES
EELINUX_TCL86_COMPAT_AUTORECONF = YES
EELINUX_TCL86_COMPAT_DEPENDENCIES = zlib
EELINUX_TCL86_COMPAT_CONF_OPTS = --disable-symbols --disable-langinfo \
	--disable-framework --includedir=/usr/include/tcl8.6
EELINUX_TCL86_COMPAT_MAKE_OPTS = binaries libraries

# The primary Tcl package owns all unversioned entry points. Install into a
# package-local tree, then copy only this ABI's names into staging and target.
define EELINUX_TCL86_COMPAT_INSTALL_TREE
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/unix DESTDIR=$(@D)/eelinux-install-root \
		install-binaries install-libraries install-headers
	$(INSTALL) -d -m 0755 $(1)/usr/bin $(1)/usr/lib $(1)/usr/lib/pkgconfig \
		$(1)/usr/include/tcl8.6
	$(INSTALL) -m 0755 $(@D)/eelinux-install-root/usr/bin/tclsh8.6 $(1)/usr/bin/tclsh8.6
	$(INSTALL) -m 0755 $(@D)/eelinux-install-root/usr/lib/libtcl8.6.so $(1)/usr/lib/
	$(INSTALL) -m 0644 $(@D)/eelinux-install-root/usr/lib/libtclstub8.6.a $(1)/usr/lib/
	cp -af $(@D)/eelinux-install-root/usr/lib/tcl8.6 $(1)/usr/lib/
	cp -af $(@D)/eelinux-install-root/usr/lib/tcl8 $(1)/usr/lib/
	cp -af $(@D)/eelinux-install-root/usr/include/tcl8.6/. $(1)/usr/include/tcl8.6/
	$(INSTALL) -m 0644 $(@D)/unix/tcl.pc $(1)/usr/lib/pkgconfig/tcl86.pc
endef

define EELINUX_TCL86_COMPAT_INSTALL_STAGING_CMDS
	$(call EELINUX_TCL86_COMPAT_INSTALL_TREE,$(STAGING_DIR))
endef

define EELINUX_TCL86_COMPAT_INSTALL_TARGET_CMDS
	$(call EELINUX_TCL86_COMPAT_INSTALL_TREE,$(TARGET_DIR))
endef

$(eval $(autotools-package))
