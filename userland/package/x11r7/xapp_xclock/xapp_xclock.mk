# Current upstream xclock; consumed by scripts/xorg_apps.py
XAPP_XCLOCK_VERSION = 1.2.1
XAPP_XCLOCK_SOURCE = xclock-1.2.1.tar.xz
XAPP_XCLOCK_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XCLOCK_LICENSE = MIT
XAPP_XCLOCK_LICENSE_FILES = COPYING

$(eval $(autotools-package))
