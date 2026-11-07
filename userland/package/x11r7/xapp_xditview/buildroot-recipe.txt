# Current upstream xditview; consumed by scripts/xorg_apps.py
XAPP_XDITVIEW_VERSION = 1.0.7
XAPP_XDITVIEW_SOURCE = xditview-1.0.7.tar.xz
XAPP_XDITVIEW_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XDITVIEW_LICENSE = MIT
XAPP_XDITVIEW_LICENSE_FILES = COPYING

$(eval $(autotools-package))
