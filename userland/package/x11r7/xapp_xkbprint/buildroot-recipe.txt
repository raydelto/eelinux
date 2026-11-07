# Current upstream xkbprint; consumed by scripts/xorg_apps.py
XAPP_XKBPRINT_VERSION = 1.0.8
XAPP_XKBPRINT_SOURCE = xkbprint-1.0.8.tar.xz
XAPP_XKBPRINT_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XKBPRINT_LICENSE = MIT
XAPP_XKBPRINT_LICENSE_FILES = COPYING

$(eval $(autotools-package))
