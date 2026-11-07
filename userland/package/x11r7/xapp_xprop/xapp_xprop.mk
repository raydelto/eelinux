# Current upstream xprop; consumed by scripts/xorg_apps.py
XAPP_XPROP_VERSION = 1.2.8
XAPP_XPROP_SOURCE = xprop-1.2.8.tar.xz
XAPP_XPROP_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XPROP_LICENSE = MIT
XAPP_XPROP_LICENSE_FILES = COPYING

$(eval $(autotools-package))
