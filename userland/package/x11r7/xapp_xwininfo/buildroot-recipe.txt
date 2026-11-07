# Current upstream xwininfo; consumed by scripts/xorg_apps.py
XAPP_XWININFO_VERSION = 1.1.7
XAPP_XWININFO_SOURCE = xwininfo-1.1.7.tar.xz
XAPP_XWININFO_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XWININFO_LICENSE = MIT
XAPP_XWININFO_LICENSE_FILES = COPYING

$(eval $(autotools-package))
