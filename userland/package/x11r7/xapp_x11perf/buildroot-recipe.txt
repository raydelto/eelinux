# Current upstream x11perf; consumed by scripts/xorg_apps.py
XAPP_X11PERF_VERSION = 1.6.1
XAPP_X11PERF_SOURCE = x11perf-1.6.1.tar.bz2
XAPP_X11PERF_SITE = https://xorg.freedesktop.org/archive/individual/app
XAPP_X11PERF_LICENSE = MIT
XAPP_X11PERF_LICENSE_FILES = COPYING

$(eval $(autotools-package))
