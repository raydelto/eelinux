# Current upstream editres; consumed by scripts/xorg_apps.py
XAPP_EDITRES_VERSION = 1.1.1
XAPP_EDITRES_SOURCE = editres-1.1.1.tar.xz
XAPP_EDITRES_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_EDITRES_LICENSE = MIT
XAPP_EDITRES_LICENSE_FILES = COPYING

$(eval $(autotools-package))
