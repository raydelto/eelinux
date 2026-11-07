# Current upstream appres; consumed by scripts/xorg_apps.py
XAPP_APPRES_VERSION = 1.0.7
XAPP_APPRES_SOURCE = appres-1.0.7.tar.xz
XAPP_APPRES_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_APPRES_LICENSE = MIT
XAPP_APPRES_LICENSE_FILES = COPYING

$(eval $(autotools-package))
