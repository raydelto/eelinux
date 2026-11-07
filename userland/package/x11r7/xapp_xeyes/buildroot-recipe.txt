# Current upstream xeyes; consumed by scripts/xorg_apps.py
XAPP_XEYES_VERSION = 1.3.1
XAPP_XEYES_SOURCE = xeyes-1.3.1.tar.xz
XAPP_XEYES_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XEYES_LICENSE = MIT
XAPP_XEYES_LICENSE_FILES = COPYING

$(eval $(autotools-package))
