# Current upstream xset; consumed by scripts/xorg_apps.py
XAPP_XSET_VERSION = 1.2.6
XAPP_XSET_SOURCE = xset-1.2.6.tar.xz
XAPP_XSET_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XSET_LICENSE = MIT
XAPP_XSET_LICENSE_FILES = COPYING

$(eval $(autotools-package))
