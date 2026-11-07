# Current upstream xclipboard; consumed by scripts/xorg_apps.py
XAPP_XCLIPBOARD_VERSION = 1.1.6
XAPP_XCLIPBOARD_SOURCE = xclipboard-1.1.6.tar.xz
XAPP_XCLIPBOARD_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XCLIPBOARD_LICENSE = MIT
XAPP_XCLIPBOARD_LICENSE_FILES = COPYING

$(eval $(autotools-package))
