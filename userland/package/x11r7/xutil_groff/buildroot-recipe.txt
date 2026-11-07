# Current upstream groff; consumed by scripts/xorg_apps.py
XUTIL_GROFF_VERSION = 1.24.2
XUTIL_GROFF_SOURCE = groff-1.24.2.tar.gz
XUTIL_GROFF_SITE = https://ftp.gnu.org/gnu/groff/
XUTIL_GROFF_LICENSE = GPL-3.0-or-later
XUTIL_GROFF_LICENSE_FILES = COPYING

$(eval $(autotools-package))
