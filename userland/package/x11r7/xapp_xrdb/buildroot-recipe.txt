# Current upstream xrdb; consumed by scripts/xorg_apps.py
XAPP_XRDB_VERSION = 1.2.3
XAPP_XRDB_SOURCE = xrdb-1.2.3.tar.xz
XAPP_XRDB_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XRDB_LICENSE = MIT
XAPP_XRDB_LICENSE_FILES = COPYING

$(eval $(autotools-package))
