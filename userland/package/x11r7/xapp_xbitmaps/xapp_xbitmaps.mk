# Current upstream xbitmaps; consumed by scripts/xorg_apps.py
XAPP_XBITMAPS_VERSION = 1.1.0
XAPP_XBITMAPS_SOURCE = xbitmaps-1.1.0.tar.bz2
XAPP_XBITMAPS_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XBITMAPS_LICENSE = MIT
XAPP_XBITMAPS_LICENSE_FILES = COPYING

$(eval $(autotools-package))
