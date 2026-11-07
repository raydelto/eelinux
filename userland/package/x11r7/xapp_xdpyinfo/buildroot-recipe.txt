# Current upstream xdpyinfo; consumed by scripts/xorg_apps.py
XAPP_XDPYINFO_VERSION = 1.4.0
XAPP_XDPYINFO_SOURCE = xdpyinfo-1.4.0.tar.xz
XAPP_XDPYINFO_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XDPYINFO_LICENSE = MIT
XAPP_XDPYINFO_LICENSE_FILES = COPYING

$(eval $(autotools-package))
