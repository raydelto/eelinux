# Current upstream viewres; consumed by scripts/xorg_apps.py
XAPP_VIEWRES_VERSION = 1.0.8
XAPP_VIEWRES_SOURCE = viewres-1.0.8.tar.xz
XAPP_VIEWRES_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_VIEWRES_LICENSE = MIT
XAPP_VIEWRES_LICENSE_FILES = COPYING

$(eval $(autotools-package))
