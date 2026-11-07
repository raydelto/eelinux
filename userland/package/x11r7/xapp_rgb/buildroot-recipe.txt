# Current upstream rgb; consumed by scripts/xorg_apps.py
XAPP_RGB_VERSION = 1.1.1
XAPP_RGB_SOURCE = rgb-1.1.1.tar.xz
XAPP_RGB_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_RGB_LICENSE = MIT
XAPP_RGB_LICENSE_FILES = COPYING

$(eval $(autotools-package))
