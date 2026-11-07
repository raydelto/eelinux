# Current upstream listres; consumed by scripts/xorg_apps.py
XAPP_LISTRES_VERSION = 1.0.7
XAPP_LISTRES_SOURCE = listres-1.0.7.tar.xz
XAPP_LISTRES_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_LISTRES_LICENSE = MIT
XAPP_LISTRES_LICENSE_FILES = COPYING

$(eval $(autotools-package))
