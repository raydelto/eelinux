# Current upstream xkbevd; consumed by scripts/xorg_apps.py
XAPP_XKBEVD_VERSION = 1.1.6
XAPP_XKBEVD_SOURCE = xkbevd-1.1.6.tar.xz
XAPP_XKBEVD_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XKBEVD_LICENSE = MIT
XAPP_XKBEVD_LICENSE_FILES = COPYING

$(eval $(autotools-package))
