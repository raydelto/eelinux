# Current upstream xpr; consumed by scripts/xorg_apps.py
XAPP_XPR_VERSION = 1.2.1
XAPP_XPR_SOURCE = xpr-1.2.1.tar.xz
XAPP_XPR_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XPR_LICENSE = MIT
XAPP_XPR_LICENSE_FILES = COPYING

$(eval $(autotools-package))
