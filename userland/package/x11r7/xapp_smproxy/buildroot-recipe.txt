# Current upstream smproxy; consumed by scripts/xorg_apps.py
XAPP_SMPROXY_VERSION = 1.0.8
XAPP_SMPROXY_SOURCE = smproxy-1.0.8.tar.xz
XAPP_SMPROXY_SITE = https://xorg.freedesktop.org/archive/individual/app/
XAPP_SMPROXY_LICENSE = MIT
XAPP_SMPROXY_LICENSE_FILES = COPYING

$(eval $(autotools-package))
