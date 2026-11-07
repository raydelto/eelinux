# Current upstream xkbcomp; consumed by scripts/xorg_apps.py
XAPP_XKBCOMP_VERSION = 1.5.0
XAPP_XKBCOMP_SOURCE = xkbcomp-1.5.0.tar.xz
XAPP_XKBCOMP_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XKBCOMP_LICENSE = MIT
XAPP_XKBCOMP_LICENSE_FILES = COPYING

$(eval $(autotools-package))
