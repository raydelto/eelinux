# Current upstream xwud; consumed by scripts/xorg_apps.py
XAPP_XWUD_VERSION = 1.0.8
XAPP_XWUD_SOURCE = xwud-1.0.8.tar.xz
XAPP_XWUD_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XWUD_LICENSE = MIT
XAPP_XWUD_LICENSE_FILES = COPYING

$(eval $(autotools-package))
