# Current upstream ico; consumed by scripts/xorg_apps.py
XAPP_ICO_VERSION = 1.0.7
XAPP_ICO_SOURCE = ico-1.0.7.tar.xz
XAPP_ICO_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_ICO_LICENSE = MIT
XAPP_ICO_LICENSE_FILES = COPYING

$(eval $(autotools-package))
