# Current upstream mkfontscale; consumed by scripts/xorg_apps.py
XAPP_MKFONTSCALE_VERSION = 1.2.4
XAPP_MKFONTSCALE_SOURCE = mkfontscale-1.2.4.tar.xz
XAPP_MKFONTSCALE_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_MKFONTSCALE_LICENSE = MIT
XAPP_MKFONTSCALE_LICENSE_FILES = COPYING

$(eval $(autotools-package))
