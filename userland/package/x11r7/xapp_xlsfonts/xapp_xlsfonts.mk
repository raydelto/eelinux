# Current upstream xlsfonts; consumed by scripts/xorg_apps.py
XAPP_XLSFONTS_VERSION = 1.0.9
XAPP_XLSFONTS_SOURCE = xlsfonts-1.0.9.tar.xz
XAPP_XLSFONTS_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XLSFONTS_LICENSE = MIT
XAPP_XLSFONTS_LICENSE_FILES = COPYING

$(eval $(autotools-package))
