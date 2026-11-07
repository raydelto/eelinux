# Current upstream setxkbmap; consumed by scripts/xorg_apps.py
XAPP_SETXKBMAP_VERSION = 1.3.5
XAPP_SETXKBMAP_SOURCE = setxkbmap-1.3.5.tar.xz
XAPP_SETXKBMAP_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_SETXKBMAP_LICENSE = MIT
XAPP_SETXKBMAP_LICENSE_FILES = COPYING

$(eval $(autotools-package))
