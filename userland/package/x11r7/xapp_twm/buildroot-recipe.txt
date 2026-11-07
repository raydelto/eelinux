# Current upstream twm; consumed by scripts/xorg_apps.py
XAPP_TWM_VERSION = 1.0.13
XAPP_TWM_SOURCE = twm-1.0.13.tar.xz
XAPP_TWM_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_TWM_LICENSE = MIT
XAPP_TWM_LICENSE_FILES = COPYING

$(eval $(autotools-package))
