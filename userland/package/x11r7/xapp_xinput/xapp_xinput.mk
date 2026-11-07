# Current upstream xinput; consumed by scripts/xorg_apps.py
XAPP_XINPUT_VERSION = 1.6.4
XAPP_XINPUT_SOURCE = xinput-1.6.4.tar.xz
XAPP_XINPUT_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XINPUT_LICENSE = MIT
XAPP_XINPUT_LICENSE_FILES = COPYING

$(eval $(autotools-package))
