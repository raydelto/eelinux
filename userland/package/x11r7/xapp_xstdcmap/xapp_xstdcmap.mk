# Current upstream xstdcmap; consumed by scripts/xorg_apps.py
XAPP_XSTDCMAP_VERSION = 1.0.6
XAPP_XSTDCMAP_SOURCE = xstdcmap-1.0.6.tar.xz
XAPP_XSTDCMAP_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XSTDCMAP_LICENSE = MIT
XAPP_XSTDCMAP_LICENSE_FILES = COPYING

$(eval $(autotools-package))
