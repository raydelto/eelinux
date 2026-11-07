# Current upstream xrefresh; consumed by scripts/xorg_apps.py
XAPP_XREFRESH_VERSION = 1.1.1
XAPP_XREFRESH_SOURCE = xrefresh-1.1.1.tar.xz
XAPP_XREFRESH_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XREFRESH_LICENSE = MIT
XAPP_XREFRESH_LICENSE_FILES = COPYING

$(eval $(autotools-package))
