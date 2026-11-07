# Current upstream xfd; consumed by scripts/xorg_apps.py
XAPP_XFD_VERSION = 1.1.6
XAPP_XFD_SOURCE = xfd-1.1.6.tar.xz
XAPP_XFD_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XFD_LICENSE = MIT
XAPP_XFD_LICENSE_FILES = COPYING

$(eval $(autotools-package))
