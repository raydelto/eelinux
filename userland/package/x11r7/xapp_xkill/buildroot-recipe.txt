# Current upstream xkill; consumed by scripts/xorg_apps.py
XAPP_XKILL_VERSION = 1.0.7
XAPP_XKILL_SOURCE = xkill-1.0.7.tar.xz
XAPP_XKILL_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XKILL_LICENSE = MIT
XAPP_XKILL_LICENSE_FILES = COPYING

$(eval $(autotools-package))
