# Current upstream libXmu; consumed by scripts/xorg_apps.py
XLIB_LIBXMU_VERSION = 1.3.1
XLIB_LIBXMU_SOURCE = libXmu-1.3.1.tar.xz
XLIB_LIBXMU_SITE = https://xorg.freedesktop.org/archive/individual/lib/
XLIB_LIBXMU_LICENSE = MIT
XLIB_LIBXMU_LICENSE_FILES = COPYING

$(eval $(autotools-package))
