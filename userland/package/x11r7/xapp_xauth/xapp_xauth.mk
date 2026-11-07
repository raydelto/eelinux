# Current upstream xauth; consumed by scripts/xorg_apps.py
XAPP_XAUTH_VERSION = 1.1.5
XAPP_XAUTH_SOURCE = xauth-1.1.5.tar.xz
XAPP_XAUTH_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XAUTH_LICENSE = MIT
XAPP_XAUTH_LICENSE_FILES = COPYING

$(eval $(autotools-package))
