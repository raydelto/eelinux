# Current upstream xman; consumed by scripts/xorg_apps.py
XAPP_XMAN_VERSION = 1.2.0
XAPP_XMAN_SOURCE = xman-1.2.0.tar.xz
XAPP_XMAN_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XMAN_LICENSE = MIT
XAPP_XMAN_LICENSE_FILES = COPYING

$(eval $(autotools-package))
