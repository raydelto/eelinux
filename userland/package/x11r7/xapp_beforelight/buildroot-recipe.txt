# Current upstream beforelight; consumed by scripts/xorg_apps.py
XAPP_BEFORELIGHT_VERSION = 1.0.6
XAPP_BEFORELIGHT_SOURCE = beforelight-1.0.6.tar.xz
XAPP_BEFORELIGHT_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_BEFORELIGHT_LICENSE = MIT
XAPP_BEFORELIGHT_LICENSE_FILES = COPYING

$(eval $(autotools-package))
