# Current upstream xsetroot; consumed by scripts/xorg_apps.py
XAPP_XSETROOT_VERSION = 1.1.4
XAPP_XSETROOT_SOURCE = xsetroot-1.1.4.tar.xz
XAPP_XSETROOT_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XSETROOT_LICENSE = MIT
XAPP_XSETROOT_LICENSE_FILES = COPYING

$(eval $(autotools-package))
