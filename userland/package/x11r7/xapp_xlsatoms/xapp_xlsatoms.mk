# Current upstream xlsatoms; consumed by scripts/xorg_apps.py
XAPP_XLSATOMS_VERSION = 1.1.5
XAPP_XLSATOMS_SOURCE = xlsatoms-1.1.5.tar.xz
XAPP_XLSATOMS_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XLSATOMS_LICENSE = MIT
XAPP_XLSATOMS_LICENSE_FILES = COPYING

$(eval $(autotools-package))
