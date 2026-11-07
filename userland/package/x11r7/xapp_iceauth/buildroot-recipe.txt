# Current upstream iceauth; consumed by scripts/xorg_apps.py
XAPP_ICEAUTH_VERSION = 1.0.11
XAPP_ICEAUTH_SOURCE = iceauth-1.0.11.tar.xz
XAPP_ICEAUTH_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_ICEAUTH_LICENSE = MIT
XAPP_ICEAUTH_LICENSE_FILES = COPYING

$(eval $(autotools-package))
