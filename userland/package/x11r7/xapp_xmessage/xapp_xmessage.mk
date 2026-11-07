# Current upstream xmessage; consumed by scripts/xorg_apps.py
XAPP_XMESSAGE_VERSION = 1.0.7
XAPP_XMESSAGE_SOURCE = xmessage-1.0.7.tar.xz
XAPP_XMESSAGE_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XMESSAGE_LICENSE = MIT
XAPP_XMESSAGE_LICENSE_FILES = COPYING

$(eval $(autotools-package))
