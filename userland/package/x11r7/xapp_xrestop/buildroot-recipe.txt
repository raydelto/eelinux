# Current upstream xrestop; consumed by scripts/xorg_apps.py
XAPP_XRESTOP_VERSION = 0.6
XAPP_XRESTOP_SOURCE = xrestop-0.6.tar.xz
XAPP_XRESTOP_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XRESTOP_LICENSE = MIT
XAPP_XRESTOP_LICENSE_FILES = COPYING

$(eval $(autotools-package))
