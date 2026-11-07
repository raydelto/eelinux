# Current upstream xhost; consumed by scripts/xorg_apps.py
XAPP_XHOST_VERSION = 1.0.10
XAPP_XHOST_SOURCE = xhost-1.0.10.tar.xz
XAPP_XHOST_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XHOST_LICENSE = MIT
XAPP_XHOST_LICENSE_FILES = COPYING

$(eval $(autotools-package))
