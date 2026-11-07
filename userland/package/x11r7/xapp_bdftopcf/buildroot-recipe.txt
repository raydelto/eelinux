# Current upstream bdftopcf; consumed by scripts/xorg_apps.py
XAPP_BDFTOPCF_VERSION = 1.1
XAPP_BDFTOPCF_SOURCE = bdftopcf-1.1.tar.bz2
XAPP_BDFTOPCF_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_BDFTOPCF_LICENSE = MIT
XAPP_BDFTOPCF_LICENSE_FILES = COPYING

$(eval $(autotools-package))
