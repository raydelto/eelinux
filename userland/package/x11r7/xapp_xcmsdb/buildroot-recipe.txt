# Current upstream xcmsdb; consumed by scripts/xorg_apps.py
XAPP_XCMSDB_VERSION = 1.0.7
XAPP_XCMSDB_SOURCE = xcmsdb-1.0.7.tar.xz
XAPP_XCMSDB_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XCMSDB_LICENSE = MIT
XAPP_XCMSDB_LICENSE_FILES = COPYING

$(eval $(autotools-package))
