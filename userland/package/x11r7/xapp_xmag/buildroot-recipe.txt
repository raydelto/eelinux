# Current upstream xmag; consumed by scripts/xorg_apps.py
XAPP_XMAG_VERSION = 1.0.8
XAPP_XMAG_SOURCE = xmag-1.0.8.tar.xz
XAPP_XMAG_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XMAG_LICENSE = MIT
XAPP_XMAG_LICENSE_FILES = COPYING

$(eval $(autotools-package))
