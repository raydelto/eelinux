# Current upstream xrandr; consumed by scripts/xorg_apps.py
XAPP_XRANDR_VERSION = 1.5.4
XAPP_XRANDR_SOURCE = xrandr-1.5.4.tar.xz
XAPP_XRANDR_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_XRANDR_LICENSE = MIT
XAPP_XRANDR_LICENSE_FILES = COPYING

$(eval $(autotools-package))
