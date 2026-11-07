# Current upstream xmore; consumed by scripts/xorg_apps.py
XAPP_XMORE_VERSION = 1.0.4
XAPP_XMORE_SOURCE = xmore-1.0.4.tar.xz
XAPP_XMORE_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XMORE_LICENSE = MIT
XAPP_XMORE_LICENSE_FILES = COPYING

$(eval $(autotools-package))
