# SPDX-License-Identifier: GPL-2.0-only
PYTHON ?= python3
WORK ?= $(HOME)/.cache/eelinux
OUTPUT ?= $(WORK)/images
TOOLCHAIN ?= $(HOME)/.local/eelinux-linux-latest
TOOLCHAIN_WORK ?= $(HOME)/.cache/eelinux-linux-latest/toolchain
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsr5900el-unknown-linux-gnu-
LIBC_SYSROOT ?= $(TOOLCHAIN)/mipsr5900el-unknown-linux-gnu/sysroot
PS2DEV ?= $(HOME)/.local/eelinux-ps2dev/ps2dev
IOP_CROSS_COMPILE ?= $(PS2DEV)/iop/bin/mipsel-none-elf-
PS2DEV_LOCK ?= toolchain/ps2dev-binary-lock.json
JOBS ?= 2
PROFILE ?= desktop
KERNEL_PROFILE ?= 7.2.8
ROOT_DEVICE ?= /dev/sda1
VIDEO_MODE ?= 576p
IMAGE_MIB ?= 512
STAGING := $(WORK)/rootfs-$(PROFILE)
KERNEL_SOURCE := $(WORK)/sources/linux-$(KERNEL_PROFILE)
BASELINE_SOURCE := $(WORK)/sources/linux-baseline
INITRAMFS := $(WORK)/initramfs-$(KERNEL_PROFILE)
GS_SOURCE ?= $(WORK)/sources/linux-$(KERNEL_PROFILE)-ps2-gs
GS_BUILD ?= $(WORK)/kernel-$(KERNEL_PROFILE)-ps2-gs
USB_KERNEL_WORK ?= $(WORK)/usb-kernel-$(VIDEO_MODE)
USB_KERNEL_DIR ?= $(OUTPUT)/usb-kernel-$(VIDEO_MODE)
ROOT_PASSWORD_HASH_FILE ?=
PASSWORD_OPTION := $(if $(ROOT_PASSWORD_HASH_FILE),--root-password-hash-file "$(ROOT_PASSWORD_HASH_FILE)",)
DESKTOP_OPTION := $(if $(filter base,$(PROFILE)),--no-desktop,)
EMULATOR_WORK ?= $(WORK)/emulator-userland
EMULATOR_STAGING ?= $(WORK)/rootfs-base
EMULATOR_REPORT ?= $(EMULATOR_WORK)/test-report.json
EMULATOR_DIRECTORY ?= $(CURDIR)/build/emulator/pcsx2
EMULATOR_HOST ?= linux
EMULATOR_KERNEL ?= $(OUTPUT)/eelinux-$(KERNEL_PROFILE)-ps2.elf
EMULATOR_ROOTFS ?= $(OUTPUT)/rootfs.ext4
EMULATOR_AUDIT ?= 0
XORG_SDK ?= $(WORK)/userland/output-desktop/staging
XORG_HOST ?= $(WORK)/userland/output-desktop/host
MEDIA_WORK ?= $(WORK)/pal-media
MEDIA_OUTPUT ?= $(CURDIR)/build/images/pal-desktop
MEDIA_KERNEL ?= $(OUTPUT)/eelinux-$(KERNEL_PROFILE)-ps2.elf
MEDIA_KERNEL_CONFIG ?= $(WORK)/kernel-$(KERNEL_PROFILE)/.config
MEDIA_KERNEL_REPORT ?= $(OUTPUT)/eelinux-$(KERNEL_PROFILE)-ps2.json
MEDIA_VIDEO_OPTIONS = --video-mode "$(VIDEO_MODE)" $(if $(filter 480p,$(VIDEO_MODE)),--fallback-kernel "$(OUTPUT)/usb-kernel-ntsc/EELINUX.ELF" --fallback-config "$(OUTPUT)/usb-kernel-ntsc/kernel.config" --fallback-report "$(OUTPUT)/usb-kernel-ntsc/kernel-report.json" --dvd-fallback-kernel "$(OUTPUT)/dvd-kernel-ntsc/EELINUX.ELF" --dvd-fallback-config "$(OUTPUT)/dvd-kernel-ntsc/kernel.config" --dvd-fallback-report "$(OUTPUT)/dvd-kernel-ntsc/kernel-report.json",)
MEDIA_OUTPUT_OPTIONS = $(MEDIA_VIDEO_OPTIONS) --work "$(MEDIA_WORK)" --output-dir "$(MEDIA_OUTPUT)"
MEDIA_OPTIONS = --kernel "$(MEDIA_KERNEL)" --kernel-config "$(MEDIA_KERNEL_CONFIG)" --kernel-report "$(MEDIA_KERNEL_REPORT)" $(MEDIA_OUTPUT_OPTIONS)
MEDIA_DVD_KERNEL_DIR ?= $(OUTPUT)/dvd-kernel$(if $(filter-out 576p,$(VIDEO_MODE)),-$(VIDEO_MODE),)
MEDIA_KERNEL_BUILD ?= $(WORK)/kernel-$(KERNEL_PROFILE)
MEDIA_DVD_WORK ?= $(WORK)/dvd-kernel$(if $(filter-out 576p,$(VIDEO_MODE)),-$(VIDEO_MODE),)
MEDIA_DVD_OPTIONS = --dvd-kernel "$(MEDIA_DVD_KERNEL_DIR)/EELINUX.ELF" --dvd-kernel-config "$(MEDIA_DVD_KERNEL_DIR)/kernel.config" --dvd-kernel-report "$(MEDIA_DVD_KERNEL_DIR)/kernel-report.json"
.PHONY: help plan check kernel-source kernel-port kernel-modernize kernel-audit toolchain ps2dev firmware userland keyboard locales dhcpcd initramfs kernel image all reference-system emulator-userland-build emulator-smoke emulator-prepare xorg-apps-update xorg-apps native-xorg usb-image-2gb kernel-gs-tree kernel-gs-reference usb-kernel dvd-kernel-pal dvd-iso-slim-pal dvd-iso-fat-pal dvd-isos-pal pal-media
help:
	@$(PYTHON) scripts/project.py help

plan:
	$(if $(filter full,$(PROFILE)),$(PYTHON) scripts/userland_full.py --plan --profile full --sysroot "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" --work "$(WORK)/userland-latest" --jobs $(JOBS),$(PYTHON) scripts/userland.py plan --profile $(PROFILE) --sysroot "$(STAGING)")

check:
	$(PYTHON) -m unittest discover -s tests -v
	$(PYTHON) -m unittest discover -s kernel -p test_integration.py -v
	$(PYTHON) scripts/project.py verify-imports

kernel-source:
	$(PYTHON) scripts/kernel.py fetch --profile $(KERNEL_PROFILE) --source "$(KERNEL_SOURCE)" --cache "$(WORK)/downloads"

# This returns nonzero with a reviewable report when hunks require manual porting.
kernel-port: kernel-source
	$(PYTHON) scripts/kernel.py port --source "$(KERNEL_SOURCE)" --report "$(WORK)/port-report.json"

kernel-modernize:
	$(PYTHON) scripts/kernel_modernize.py --source "$(KERNEL_SOURCE)" --patch-output "$(WORK)/7.2.8-api.patch"

kernel-audit:
	$(PYTHON) scripts/kernel.py audit --profile $(KERNEL_PROFILE) --source "$(KERNEL_SOURCE)" --report "$(WORK)/kernel-audit.json"

toolchain:
	$(PYTHON) scripts/kernel.py fetch --profile baseline --source "$(BASELINE_SOURCE)" --cache "$(WORK)/downloads"
	$(PYTHON) toolchain/bootstrap.py build --work "$(TOOLCHAIN_WORK)" --prefix "$(TOOLCHAIN)" --kernel-source "$(BASELINE_SOURCE)" --jobs $(JOBS)

firmware:
	$(PYTHON) scripts/kernel.py firmware --source "$(WORK)/sources/iopmod" --destination "$(WORK)/firmware" --cross-prefix "$(IOP_CROSS_COMPILE)" --jobs $(JOBS)

ps2dev:
	$(PYTHON) scripts/install_ps2dev.py --directory "$(PS2DEV)/.." --cache "$(WORK)/downloads" --lock "$(PS2DEV_LOCK)"

userland:
	EELINUX_QEMU_WORK="$(EMULATOR_WORK)" $(if $(filter full,$(PROFILE)),$(PYTHON) scripts/userland_full.py --profile full --sysroot "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" --work "$(WORK)/userland-latest" --jobs $(JOBS),$(PYTHON) scripts/userland.py build --sysroot "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" --work "$(WORK)/userland" --jobs $(JOBS) --profile $(PROFILE))

# Supplement a previously built GNU stage without changing its locked package build.
keyboard:
	$(PYTHON) scripts/keyboard.py build --work "$(WORK)/keyboard-build" --toolchain-prefix "$(CROSS_COMPILE)" --staging "$(WORK)/keyboard-payload" --jobs $(JOBS)
	$(PYTHON) scripts/keyboard.py install --payload "$(WORK)/keyboard-payload" --staging "$(STAGING)"

# Regenerate matching native libc locale data in an existing stage.
locales:
	$(PYTHON) scripts/locales.py --staging "$(STAGING)" --sysroot "$(LIBC_SYSROOT)" --work "$(WORK)/utf8-locales" --emulator-work "$(EMULATOR_WORK)" $(if $(filter base,$(PROFILE)),,--x11-sysroot "$(XORG_SDK)")

dhcpcd:
	$(PYTHON) scripts/dhcpcd.py build --work "$(WORK)/dhcpcd-build" --toolchain-prefix "$(CROSS_COMPILE)" --staging "$(WORK)/dhcpcd-payload" --jobs $(JOBS)
	$(PYTHON) scripts/dhcpcd.py install --payload "$(WORK)/dhcpcd-payload" --staging "$(STAGING)"

initramfs:
	$(PYTHON) scripts/initramfs.py prepare --directory "$(INITRAMFS)" --toolchain-prefix "$(CROSS_COMPILE)" --root-device "$(ROOT_DEVICE)"

kernel: kernel-audit initramfs
	$(PYTHON) scripts/kernel.py build --profile $(KERNEL_PROFILE) --source "$(KERNEL_SOURCE)" --output "$(WORK)/kernel-$(KERNEL_PROFILE)" --initramfs "$(INITRAMFS)" --cross-prefix "$(CROSS_COMPILE)" --firmware "$(WORK)/firmware" --modules-root "$(STAGING)" --root-device "$(ROOT_DEVICE)" --video-mode "$(VIDEO_MODE)" --jobs $(JOBS) --artifact "$(OUTPUT)/eelinux-$(KERNEL_PROFILE)-ps2.elf"

image:
	$(PYTHON) scripts/rootfs.py --staging "$(STAGING)" --output-dir "$(OUTPUT)" --size-mib $(IMAGE_MIB) --hostname eelinux --video-mode "$(VIDEO_MODE)" $(DESKTOP_OPTION) $(PASSWORD_OPTION)

# Refresh explicitly; ordinary builds consume checksum-pinned releases.
xorg-apps-update:
	$(PYTHON) scripts/xorg_apps.py resolve

xorg-apps:
	$(PYTHON) scripts/xorg_apps.py build --work "$(WORK)/xorg-applications" --sysroot "$(XORG_SDK)" --staging "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" --jobs $(JOBS)

native-xorg:
	$(PYTHON) scripts/native_xorg.py --work "$(WORK)/xorg-native" --sysroot "$(XORG_SDK)" --host-tools "$(XORG_HOST)" --staging "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" --jobs $(JOBS)

# Media kernels: native-GS tree, its reference build, then the USB kernel.
# The source gate is still closed; these targets build anyway, as the released
# media kernels were, and record source_gate_passed=false in their reports.
kernel-gs-tree:
	$(PYTHON) scripts/kernel_gs_tree.py --source "$(KERNEL_SOURCE)" --output "$(GS_SOURCE)"

kernel-gs-reference: initramfs
	$(PYTHON) scripts/kernel.py build --allow-open-gate --profile $(KERNEL_PROFILE) --source "$(GS_SOURCE)" --output "$(GS_BUILD)" --initramfs "$(INITRAMFS)" --cross-prefix "$(CROSS_COMPILE)" --firmware "$(WORK)/firmware" --modules-root "$(GS_BUILD)-modules" --root-device "$(ROOT_DEVICE)" --video-mode "$(VIDEO_MODE)" --jobs $(JOBS) --artifact "$(GS_BUILD)-artifact/eelinux-gs-reference.elf"

usb-kernel:
	$(PYTHON) scripts/kernel_usb.py --source "$(GS_SOURCE)" --base-output "$(GS_BUILD)" --work "$(USB_KERNEL_WORK)" --output-dir "$(USB_KERNEL_DIR)" --cross-prefix "$(CROSS_COMPILE)" --video-mode "$(VIDEO_MODE)" --jobs $(JOBS)

# Packaging uses an existing compiled desktop and a PAL /dev/sda2 kernel.
# No tests, qualification services or diagnostics are packaged or run here.
usb-image-2gb:
	$(PYTHON) scripts/ps2_media.py usb $(MEDIA_OPTIONS) --staging "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" $(PASSWORD_OPTION)

dvd-kernel-pal:
	$(PYTHON) scripts/kernel_dvd.py --source "$(KERNEL_SOURCE)" --base-output "$(MEDIA_KERNEL_BUILD)" --work "$(MEDIA_DVD_WORK)" --output-dir "$(MEDIA_DVD_KERNEL_DIR)" --cross-prefix "$(CROSS_COMPILE)" --iop-prefix "$(IOP_CROSS_COMPILE)" --video-mode "$(VIDEO_MODE)" --jobs $(JOBS)

dvd-iso-slim-pal:
	$(PYTHON) scripts/ps2_media.py dvd --variant slim $(MEDIA_OUTPUT_OPTIONS) $(MEDIA_DVD_OPTIONS) --staging "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" $(PASSWORD_OPTION)

# Emits separate 2.10/2.12 and 3.04 firmware variants, with explicit PAL limits.
dvd-iso-fat-pal:
	$(PYTHON) scripts/ps2_media.py dvd --variant fat $(MEDIA_OUTPUT_OPTIONS) $(MEDIA_DVD_OPTIONS) --staging "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" $(PASSWORD_OPTION)

dvd-isos-pal:
	$(PYTHON) scripts/ps2_media.py dvd --variant all $(MEDIA_OUTPUT_OPTIONS) $(MEDIA_DVD_OPTIONS) --staging "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" $(PASSWORD_OPTION)

pal-media:
	$(PYTHON) scripts/ps2_media.py all $(MEDIA_OPTIONS) $(MEDIA_DVD_OPTIONS) --staging "$(STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" $(PASSWORD_OPTION)

# Linux-user runs selected GNU programs, not the PS2 kernel or PID 1.
emulator-userland-build:
	$(PYTHON) scripts/emulator_userland.py build --work "$(EMULATOR_WORK)" --jobs $(JOBS)

emulator-smoke:
	$(PYTHON) scripts/emulator_userland.py test --work "$(EMULATOR_WORK)" --staging "$(EMULATOR_STAGING)" --toolchain-prefix "$(CROSS_COMPILE)" --timeout 15 --report "$(EMULATOR_REPORT)"

# Experimental PCSX2 setup only; an empty session directory is required.
emulator-prepare:
	$(PYTHON) scripts/emulator.py prepare --kernel "$(EMULATOR_KERNEL)" --rootfs "$(EMULATOR_ROOTFS)" --directory "$(EMULATOR_DIRECTORY)" --host "$(EMULATOR_HOST)" $(if $(filter 1,$(EMULATOR_AUDIT)),--audit-kernel,)

# Requested 7.2.8 build is gated before downloading/building the distribution.
# No old kernel is silently substituted when the modern port is incomplete.
all: kernel-audit
	$(MAKE) toolchain
	$(MAKE) ps2dev
	$(MAKE) firmware
	$(MAKE) userland
	$(MAKE) kernel
	$(MAKE) image

# Explicit old-kernel reference, useful for bring-up; hardware testing pending.
reference-system:
	$(MAKE) kernel-source KERNEL_PROFILE=baseline
	$(MAKE) all KERNEL_PROFILE=baseline
