# PNG dependency for native xcursorgen
XLIB_LIBPNG_VERSION = 1.6.59
XLIB_LIBPNG_SOURCE = libpng-1.6.59.tar.xz
XLIB_LIBPNG_SITE = https://download.sourceforge.net/libpng
XLIB_LIBPNG_LICENSE = libpng-2.0
XLIB_LIBPNG_LICENSE_FILES = LICENSE

$(eval $(autotools-package))
