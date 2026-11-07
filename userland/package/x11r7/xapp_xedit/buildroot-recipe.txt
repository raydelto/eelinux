# Current upstream xedit; consumed by scripts/xorg_apps.py
XAPP_XEDIT_VERSION = 1.2.5
XAPP_XEDIT_SOURCE = xedit-1.2.5.tar.xz
XAPP_XEDIT_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XEDIT_LICENSE = MIT
XAPP_XEDIT_LICENSE_FILES = COPYING

$(eval $(autotools-package))
