# Current upstream bitmap; consumed by scripts/xorg_apps.py
XAPP_BITMAP_VERSION = 1.1.2
XAPP_BITMAP_SOURCE = bitmap-1.1.2.tar.xz
XAPP_BITMAP_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_BITMAP_LICENSE = MIT
XAPP_BITMAP_LICENSE_FILES = COPYING

$(eval $(autotools-package))
