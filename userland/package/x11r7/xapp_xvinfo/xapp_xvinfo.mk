# Current upstream xvinfo; consumed by scripts/xorg_apps.py
XAPP_XVINFO_VERSION = 1.1.6
XAPP_XVINFO_SOURCE = xvinfo-1.1.6.tar.xz
XAPP_XVINFO_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_XVINFO_LICENSE = MIT
XAPP_XVINFO_LICENSE_FILES = COPYING

$(eval $(autotools-package))
