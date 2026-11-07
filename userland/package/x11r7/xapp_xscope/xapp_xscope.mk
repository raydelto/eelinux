# Current upstream xscope; consumed by scripts/xorg_apps.py
XAPP_XSCOPE_VERSION = 1.4.5
XAPP_XSCOPE_SOURCE = xscope-1.4.5.tar.xz
XAPP_XSCOPE_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XSCOPE_LICENSE = MIT
XAPP_XSCOPE_LICENSE_FILES = COPYING

$(eval $(autotools-package))
