# Current upstream xkbutils; consumed by scripts/xorg_apps.py
XAPP_XKBUTILS_VERSION = 1.0.7
XAPP_XKBUTILS_SOURCE = xkbutils-1.0.7.tar.xz
XAPP_XKBUTILS_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XKBUTILS_LICENSE = MIT
XAPP_XKBUTILS_LICENSE_FILES = COPYING

$(eval $(autotools-package))
