# Current upstream xlsclients; consumed by scripts/xorg_apps.py
XAPP_XLSCLIENTS_VERSION = 1.1.6
XAPP_XLSCLIENTS_SOURCE = xlsclients-1.1.6.tar.xz
XAPP_XLSCLIENTS_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XLSCLIENTS_LICENSE = MIT
XAPP_XLSCLIENTS_LICENSE_FILES = COPYING

$(eval $(autotools-package))
