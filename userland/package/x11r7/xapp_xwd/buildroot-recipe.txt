# Current upstream xwd; consumed by scripts/xorg_apps.py
XAPP_XWD_VERSION = 1.0.10
XAPP_XWD_SOURCE = xwd-1.0.10.tar.xz
XAPP_XWD_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XWD_LICENSE = MIT
XAPP_XWD_LICENSE_FILES = COPYING

$(eval $(autotools-package))
