# Current upstream oclock; consumed by scripts/xorg_apps.py
XAPP_OCLOCK_VERSION = 1.0.6
XAPP_OCLOCK_SOURCE = oclock-1.0.6.tar.xz
XAPP_OCLOCK_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_OCLOCK_LICENSE = MIT
XAPP_OCLOCK_LICENSE_FILES = COPYING

$(eval $(autotools-package))
