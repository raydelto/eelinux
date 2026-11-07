# Current upstream libXv; consumed by scripts/xorg_apps.py
XLIB_LIBXV_VERSION = 1.0.13
XLIB_LIBXV_SOURCE = libXv-1.0.13.tar.xz
XLIB_LIBXV_SITE = https://xorg.freedesktop.org/archive/individual/lib
XLIB_LIBXV_LICENSE = MIT
XLIB_LIBXV_LICENSE_FILES = COPYING

$(eval $(autotools-package))
