# Current upstream xinit; consumed by scripts/xorg_apps.py
XAPP_XINIT_VERSION = 1.4.4
XAPP_XINIT_SOURCE = xinit-1.4.4.tar.xz
XAPP_XINIT_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XINIT_LICENSE = MIT
XAPP_XINIT_LICENSE_FILES = COPYING

$(eval $(autotools-package))
