# Current upstream xlogo; consumed by scripts/xorg_apps.py
XAPP_XLOGO_VERSION = 1.0.7
XAPP_XLOGO_SOURCE = xlogo-1.0.7.tar.xz
XAPP_XLOGO_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XLOGO_LICENSE = MIT
XAPP_XLOGO_LICENSE_FILES = COPYING

$(eval $(autotools-package))
