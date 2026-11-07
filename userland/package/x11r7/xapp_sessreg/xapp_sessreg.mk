# Current upstream sessreg; consumed by scripts/xorg_apps.py
XAPP_SESSREG_VERSION = 1.1.4
XAPP_SESSREG_SOURCE = sessreg-1.1.4.tar.xz
XAPP_SESSREG_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_SESSREG_LICENSE = MIT
XAPP_SESSREG_LICENSE_FILES = COPYING

$(eval $(autotools-package))
