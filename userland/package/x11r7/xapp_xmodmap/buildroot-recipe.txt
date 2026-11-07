# Current upstream xmodmap; consumed by scripts/xorg_apps.py
XAPP_XMODMAP_VERSION = 1.0.12
XAPP_XMODMAP_SOURCE = xmodmap-1.0.12.tar.xz
XAPP_XMODMAP_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XMODMAP_LICENSE = MIT
XAPP_XMODMAP_LICENSE_FILES = COPYING

$(eval $(autotools-package))
