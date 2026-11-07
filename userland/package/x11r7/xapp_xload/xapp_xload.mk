# Current upstream xload; consumed by scripts/xorg_apps.py
XAPP_XLOAD_VERSION = 1.2.2
XAPP_XLOAD_SOURCE = xload-1.2.2.tar.xz
XAPP_XLOAD_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XLOAD_LICENSE = MIT
XAPP_XLOAD_LICENSE_FILES = COPYING

$(eval $(autotools-package))
