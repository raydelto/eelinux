# Current upstream xgc; consumed by scripts/xorg_apps.py
XAPP_XGC_VERSION = 1.0.7
XAPP_XGC_SOURCE = xgc-1.0.7.tar.xz
XAPP_XGC_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_XGC_LICENSE = MIT
XAPP_XGC_LICENSE_FILES = COPYING

$(eval $(autotools-package))
