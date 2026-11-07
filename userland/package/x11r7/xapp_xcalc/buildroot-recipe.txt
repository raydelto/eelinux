# Current upstream xcalc; consumed by scripts/xorg_apps.py
XAPP_XCALC_VERSION = 1.1.3
XAPP_XCALC_SOURCE = xcalc-1.1.3.tar.xz
XAPP_XCALC_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XCALC_LICENSE = MIT
XAPP_XCALC_LICENSE_FILES = COPYING

$(eval $(autotools-package))
