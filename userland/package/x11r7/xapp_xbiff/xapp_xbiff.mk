# Current upstream xbiff; consumed by scripts/xorg_apps.py
XAPP_XBIFF_VERSION = 1.0.6
XAPP_XBIFF_SOURCE = xbiff-1.0.6.tar.xz
XAPP_XBIFF_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XBIFF_LICENSE = MIT
XAPP_XBIFF_LICENSE_FILES = COPYING

$(eval $(autotools-package))
