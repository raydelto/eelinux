# Current upstream rendercheck; consumed by scripts/xorg_apps.py
XAPP_RENDERCHECK_VERSION = 1.5
XAPP_RENDERCHECK_SOURCE = rendercheck-1.5.tar.bz2
XAPP_RENDERCHECK_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_RENDERCHECK_LICENSE = MIT
XAPP_RENDERCHECK_LICENSE_FILES = COPYING

$(eval $(autotools-package))
