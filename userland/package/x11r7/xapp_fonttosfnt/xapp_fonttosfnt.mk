# Current upstream fonttosfnt; consumed by scripts/xorg_apps.py
XAPP_FONTTOSFNT_VERSION = 1.2.5
XAPP_FONTTOSFNT_SOURCE = fonttosfnt-1.2.5.tar.xz
XAPP_FONTTOSFNT_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_FONTTOSFNT_LICENSE = MIT
XAPP_FONTTOSFNT_LICENSE_FILES = COPYING

$(eval $(autotools-package))
