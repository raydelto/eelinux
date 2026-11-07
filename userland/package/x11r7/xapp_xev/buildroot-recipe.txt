# Current upstream xev; consumed by scripts/xorg_apps.py
XAPP_XEV_VERSION = 1.2.7
XAPP_XEV_SOURCE = xev-1.2.7.tar.xz
XAPP_XEV_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XEV_LICENSE = MIT
XAPP_XEV_LICENSE_FILES = COPYING

$(eval $(autotools-package))
