# Current upstream xsm; consumed by scripts/xorg_apps.py
XAPP_XSM_VERSION = 1.0.6
XAPP_XSM_SOURCE = xsm-1.0.6.tar.xz
XAPP_XSM_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XSM_LICENSE = MIT
XAPP_XSM_LICENSE_FILES = COPYING

$(eval $(autotools-package))
