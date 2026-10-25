# SPDX-License-Identifier: GPL-2.0-only
#
# Dedicated OpenWrt 24.10 device definition for the Sphairon/Zyxel
# Speedlink 5501 (SPHSL550X).

# Keep the compatibility helper local because the SL5501 migrated to the
# mainline DSA-based XRX200 switch stack.
define Device/dsa-migration
  DEVICE_COMPAT_VERSION := 1.1
  DEVICE_COMPAT_MESSAGE := Config cannot be migrated from swconfig to DSA
endef

# Build a valid, writable 256 KiB uboot_cfg partition.  The first 8 KiB are
# a CRC-protected U-Boot environment; the remainder stays erased (0xff).
# The OEM SL5501 base MAC is the initial value and can be changed at any time
# from U-Boot with `setenv ethaddr XX:XX:XX:XX:XX:XX; saveenv`.
define Build/sl5501-uboot-cfg
	@set -e; \
		$(STAGING_DIR_HOST)/bin/python3 $(TOPDIR)/scripts/mk-sl5501-uboot-env.py \
			--ethaddr 1C:74:0D:BA:28:10 --output "$@"; \
		[ "$$(wc -c < "$@")" -eq 262144 ] || { echo "ERROR: SL5501 uboot_cfg must be 262144 bytes" >&2; exit 1; }
endef

# Build a complete 64 MiB programmer image directly from the artifacts that
# OpenWrt has just built for this device.  Unlike the earlier debug image,
# uboot_cfg is populated with a valid environment, so U-Boot no longer emits
# "bad CRC" on first boot and starts with the OEM base MAC.
define Build/sl5501-fullflash-spi
	@set -e; \
		uboot="$(UBOOT_PATH)"; \
		firmware="$(BIN_DIR)/$(DEVICE_IMG_PREFIX)-squashfs-factory.bin"; \
		envtmp="$@.uboot_cfg.tmp"; \
		[ -s "$$uboot" ] || { echo "ERROR: SL5501 SFSPL U-Boot missing: $$uboot" >&2; exit 1; }; \
		[ -s "$$firmware" ] || { echo "ERROR: SL5501 factory image missing: $$firmware" >&2; exit 1; }; \
		$(STAGING_DIR_HOST)/bin/python3 $(TOPDIR)/scripts/mk-sl5501-uboot-env.py \
			--ethaddr 1C:74:0D:BA:28:10 --output "$$envtmp"; \
		uboot_size=$$(wc -c < "$$uboot"); \
		firmware_size=$$(wc -c < "$$firmware"); \
		[ "$$uboot_size" -le 524288 ] || { echo "ERROR: SL5501 U-Boot is $$uboot_size bytes, max 524288" >&2; exit 1; }; \
		[ "$$(wc -c < "$$envtmp")" -eq 262144 ] || { echo "ERROR: SL5501 uboot_cfg has wrong size" >&2; exit 1; }; \
		[ "$$firmware_size" -le 16252928 ] || { echo "ERROR: SL5501 firmware is $$firmware_size bytes, max 16252928" >&2; exit 1; }; \
		rm -f "$@"; \
		dd if=/dev/zero bs=1048576 count=64 2>/dev/null | tr '\000' '\377' > "$@"; \
		dd if="$$uboot" of="$@" bs=1 seek=0 conv=notrunc 2>/dev/null; \
		dd if="$$envtmp" of="$@" bs=1 seek=524288 conv=notrunc 2>/dev/null; \
		dd if="$$firmware" of="$@" bs=1 seek=786432 conv=notrunc 2>/dev/null; \
		image_size=$$(wc -c < "$@"); \
		[ "$$image_size" -eq 67108864 ] || { echo "ERROR: SL5501 fullflash size is $$image_size, expected 67108864" >&2; exit 1; }; \
		echo "SL5501 fullflash SPI: 64 MiB"; \
		echo "  0x00000000-0x0007ffff  uboot_fix  ($$uboot_size bytes used)"; \
		echo "  0x00080000-0x000bffff  uboot_cfg  (valid env; ethaddr=1C:74:0D:BA:28:10)"; \
		echo "  0x000c0000-0x0103ffff  firmware   ($$firmware_size bytes used)"; \
		echo "  0x01040000-0x03ffffff  data       (erased/0xff)"; \
		rm -f "$$envtmp"
endef

define Device/sphairon_sl5501
  $(Device/dsa-migration)
  DEVICE_VENDOR := Sphairon/Zyxel
  DEVICE_MODEL := Speedlink 5501
  BOARD_NAME := SPHSL550X

  # Sphairon NTLB layout from the original bootloader:
  #   0x00000000-0x0007ffff  uboot_fix  (512 KiB)
  #   0x00080000-0x000bffff  uboot_cfg  (256 KiB)
  #   0x000c0000-0x0103ffff  firmware   (15.5 MiB)
  #   0x01040000-0x03ffffff  data       (47.75 MiB)
  IMAGE_SIZE := 15872k

  IMAGES := sysupgrade.bin factory.bin
  IMAGE/factory.bin := append-kernel | append-rootfs | pad-rootfs | check-size

  # OpenWrt-built replacement for the fixed 512 KiB uboot_fix partition.
  UBOOT_PATH := $$(STAGING_DIR_IMAGE)/sl5501_sfspl-u-boot.ltq.lzo.sfspl

  # All artifacts are generated automatically by the normal OpenWrt image
  # build. uboot-cfg.bin is a standalone 256 KiB environment image and
  # fullflash-spi.bin is suitable for writing to the complete 64 MiB NOR.
  ARTIFACTS := u-boot-sfspl.bin uboot-cfg.bin fullflash-spi.bin
  ARTIFACT/u-boot-sfspl.bin := append-uboot | check-size 512k
  ARTIFACT/uboot-cfg.bin := sl5501-uboot-cfg
  ARTIFACT/fullflash-spi.bin := sl5501-fullflash-spi

  DEVICE_PACKAGES := \
    kmod-rt3662-inic wifi-scripts iwinfo wpad-basic-mbedtls iw wireless-regdb \
    uboot-envtools \
    kmod-usb-dwc2 kmod-usb-ledtrig-usbport \
    kmod-ltq-tapi kmod-ltq-vmmc

  SUPPORTED_DEVICES += SPHSL550X sphairon,sl5501
endef
TARGET_DEVICES += sphairon_sl5501
