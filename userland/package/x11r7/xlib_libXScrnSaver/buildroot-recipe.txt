# Current upstream libXScrnSaver; consumed by scripts/xorg_apps.py
XLIB_LIBXSCRNSAVER_VERSION = 1.2.5
XLIB_LIBXSCRNSAVER_SOURCE = libXScrnSaver-1.2.5.tar.xz
XLIB_LIBXSCRNSAVER_SITE = https://xorg.freedesktop.org/archive/individual/lib/
XLIB_LIBXSCRNSAVER_LICENSE = MIT
XLIB_LIBXSCRNSAVER_LICENSE_FILES = COPYING

$(eval $(autotools-package))
