# Current releases are source- and license-pinned before recipe evaluation by
# latest_packages.py. Keep the historical audited tree available only when an
# explicit local-source reproduction is requested; otherwise it would silently
# replace the selected release with SysVinit 3.13.
ifeq ($(EELINUX_USE_VENDORED_SYSVINIT),y)
SYSVINIT_SITE_METHOD = local
SYSVINIT_OVERRIDE_SRCDIR = $(BR2_EXTERNAL_EELINUX_PATH)/sysvinit
endif
