# Current upstream xfontsel; consumed by scripts/xorg_apps.py
XAPP_XFONTSEL_VERSION = 1.1.2
XAPP_XFONTSEL_SOURCE = xfontsel-1.1.2.tar.xz
XAPP_XFONTSEL_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XFONTSEL_LICENSE = MIT
XAPP_XFONTSEL_LICENSE_FILES = COPYING

$(eval $(autotools-package))
