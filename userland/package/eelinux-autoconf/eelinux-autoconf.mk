EELINUX_AUTOCONF_VERSION = 2.72
EELINUX_AUTOCONF_SOURCE = autoconf-$(EELINUX_AUTOCONF_VERSION).tar.xz
EELINUX_AUTOCONF_SITE = https://ftp.gnu.org/gnu/autoconf
EELINUX_AUTOCONF_LICENSE = GPL-3.0+ with exceptions
EELINUX_AUTOCONF_LICENSE_FILES = COPYINGv3 COPYING.EXCEPTION
EELINUX_AUTOCONF_DEPENDENCIES = eelinux-m4 perl host-m4
EELINUX_AUTOCONF_CONF_ENV = EMACS=no ac_cv_path_PERL=/usr/bin/perl \
	ac_cv_path_M4=/usr/bin/m4 ac_cv_prog_gnu_m4_gnu=no
$(eval $(autotools-package))
