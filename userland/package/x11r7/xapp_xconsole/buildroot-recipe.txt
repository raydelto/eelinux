# Current upstream xconsole; consumed by scripts/xorg_apps.py
XAPP_XCONSOLE_VERSION = 1.1.1
XAPP_XCONSOLE_SOURCE = xconsole-1.1.1.tar.xz
XAPP_XCONSOLE_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_XCONSOLE_LICENSE = MIT
XAPP_XCONSOLE_LICENSE_FILES = COPYING

$(eval $(autotools-package))
