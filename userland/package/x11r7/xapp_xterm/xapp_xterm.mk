# Current upstream xterm; consumed by scripts/xorg_apps.py
XAPP_XTERM_VERSION = 412
XAPP_XTERM_SOURCE = xterm-412.tgz
XAPP_XTERM_SITE = https://invisible-island.net/archives/xterm/current/
XAPP_XTERM_LICENSE = MIT
XAPP_XTERM_LICENSE_FILES = COPYING

$(eval $(autotools-package))
