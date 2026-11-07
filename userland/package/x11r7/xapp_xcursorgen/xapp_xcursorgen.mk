# Current upstream xcursorgen; consumed by scripts/xorg_apps.py
XAPP_XCURSORGEN_VERSION = 1.0.9
XAPP_XCURSORGEN_SOURCE = xcursorgen-1.0.9.tar.xz
XAPP_XCURSORGEN_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_XCURSORGEN_LICENSE = MIT
XAPP_XCURSORGEN_LICENSE_FILES = COPYING

$(eval $(autotools-package))
