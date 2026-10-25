#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
DTS="$ROOT/target/linux/lantiq/files/arch/mips/boot/dts/lantiq/vr9_sphairon_sl5501.dts"
VR9MK="$ROOT/target/linux/lantiq/image/vr9.mk"
NET="$ROOT/target/linux/lantiq/xrx200/base-files/etc/board.d/02_network"
LEDS="$ROOT/target/linux/lantiq/xrx200/base-files/etc/board.d/01_leds"
UPGRADE="$ROOT/target/linux/lantiq/xrx200/base-files/lib/upgrade/platform.sh"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
FW="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.bin"
DEF="$ROOT/configs/sl5501_defconfig"
UBOOT_MK="$ROOT/package/boot/uboot-lantiq/Makefile"
UBOOT_PATCH="$ROOT/package/boot/uboot-lantiq/patches/0117-MIPS-add-board-support-for-Sphairon-SL5501.patch"
UBOOT_DDR_GENERIC="$ROOT/package/boot/uboot-lantiq/patches/0035-MIPS-vrx200-apply-board-calibrated-DDR-delays.patch"
UBOOT_SPI_DIAG="$ROOT/package/boot/uboot-lantiq/patches/0036-MIPS-lantiq-SPL-verify-SPI-source-before-DDR.patch"
UBOOT_DDR_SL5501="$ROOT/package/boot/uboot-lantiq/patches/0118-MIPS-sl5501-use-Sphairon-trained-DDR-delays.patch"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "OK: $*"; }
require() { grep -F "$2" "$1" >/dev/null || fail "$3"; }

[ -f "$DTS" ] || fail "SL5501 DTS missing"
[ -f "$DEF" ] || fail "SL5501 defconfig missing"
[ -f "$FW" ] || fail "SL5501 iNIC firmware missing"

require "$DTS" 'reg = <0x0 0x08000000>;' '128 MiB RAM is not encoded in DTS'
require "$DTS" 'reg = <0x000c0000 0x00f80000>;' '15.5 MiB firmware partition mismatch'
require "$DTS" 'reg = <0x01040000 0x02fc0000>;' 'data partition mismatch'
require "$DTS" 'power-gpios = <&gpio 37 GPIO_ACTIVE_HIGH>;' 'WLAN power GPIO mismatch'
require "$DTS" 'reset-gpios = <&gpio 15 GPIO_ACTIVE_LOW>;' 'WLAN reset GPIO mismatch'
require "$DTS" 'label = "wifi";' 'GSWIP iNIC port missing'
require "$DTS" 'lantiq,gphy-mode = <GPHY_MODE_FE>;' 'GPHY0 FE mode missing'
require "$DTS" 'lantiq,gphy-mode = <GPHY_MODE_GE>;' 'GPHY1 GE mode missing'
require "$DTS" 'gpios = <&gpio 41 GPIO_ACTIVE_HIGH>;' 'USB0 power GPIO mismatch'
require "$DTS" 'gpios = <&gpio 33 GPIO_ACTIVE_HIGH>;' 'USB1 power GPIO mismatch'
pass "DTS board wiring and NTLB layout"

require "$VR9MK" 'define Device/sphairon_sl5501' 'image device definition missing'
require "$VR9MK" 'IMAGE_SIZE := 15872k' 'image size does not match NTLB firmware slot'
require "$VR9MK" 'IMAGE/factory.bin := append-kernel | append-rootfs | pad-rootfs | check-size' 'raw NTLB factory image pipeline missing'
require "$VR9MK" 'kmod-rt3662-inic' 'RT3662 package not selected by device'
pass "image definition"
require "$VR9MK" 'ARTIFACTS := u-boot-sfspl.bin uboot-cfg.bin fullflash-spi.bin' 'fullflash SPI artifact is not enabled'
require "$VR9MK" 'ARTIFACT/fullflash-spi.bin := sl5501-fullflash-spi' 'fullflash SPI recipe missing'
require "$VR9MK" 'define Build/sl5501-fullflash-spi' 'fullflash SPI build command missing'
require "$VR9MK" 'seek=786432' 'firmware is not written at SPI offset 0x000c0000'
require "$VR9MK" '67108864' '64 MiB fullflash size check missing'
pass "automatic 64 MiB SPI fullflash image pipeline"

require "$NET" 'sphairon,sl5501)' 'board network integration missing'
require "$NET" 'ucidef_set_interfaces_lan_wan "lan1 lan2 lan3 lan4 wifi" "wan"' 'LAN/WAN/iNIC bridge map missing'
require "$NET" 'mtd_get_mac_ascii uboot_cfg ethaddr' 'U-Boot MAC extraction missing'
require "$LEDS" 'sphairon,sl5501)' 'LED board integration missing'
require "$UPGRADE" 'sphairon,sl5501)' 'explicit sysupgrade board integration missing'
pass "base-files board integration"

require "$PKG" 'define KernelPackage/rt3662-inic' 'RT3662 kernel package missing'
require "$PKG" '+kmod-cfg80211' 'cfg80211 dependency missing'
FW_SIZE=$(wc -c < "$FW" | tr -d ' ')
[ "$FW_SIZE" = 576932 ] || fail "OEM iNIC firmware size is $FW_SIZE, expected 576932 (576892 payload + 40-byte CRC record)"
if command -v sha256sum >/dev/null 2>&1; then
  FW_SHA=$(sha256sum "$FW" | awk '{print $1}')
  [ "$FW_SHA" = f927ebf5a4ac0eebef037da11490736a6a6f1adc8e900cf42781d76d1ed010cc ] || \
    fail "OEM iNIC firmware SHA-256 mismatch"
fi
pass "RT3662 cfg80211 package and OEM firmware"
sh "$ROOT/scripts/test-sl5501-rt3662-bootstrap.sh"

require "$DEF" 'CONFIG_TARGET_lantiq_xrx200_DEVICE_sphairon_sl5501=y' 'defconfig does not select SL5501'
require "$DEF" 'CONFIG_PACKAGE_kmod-rt3662-inic=y' 'defconfig does not select RT3662 driver'
require "$DEF" 'CONFIG_PACKAGE_u-boot-sl5501_sfspl=y' 'defconfig does not select SL5501 SPI U-Boot'
require "$DEF" 'CONFIG_PACKAGE_u-boot-sl5501_ram=y' 'defconfig does not select SL5501 RAM recovery U-Boot'
pass "defconfig"

require "$UBOOT_MK" 'UBOOT_TARGETS:=' 'U-Boot target list missing'
require "$UBOOT_MK" 'sl5501_ram sl5501_sfspl' 'SL5501 U-Boot variants missing'
if grep -A2 '^UBOOT_TARGETS:=' "$UBOOT_MK" | grep -Eq 'arv|easy|fb3370|vgv|p2812|bthome'; then
  fail "U-Boot target list still exposes non-SL5501 variants"
fi
require "$UBOOT_PATCH" 'sl5501_ram' 'SL5501 RAM boards.cfg entry missing'
require "$UBOOT_PATCH" 'sl5501_sfspl' 'SL5501 SFSPL boards.cfg entry missing'
require "$UBOOT_MK" '$(call CompressVR9Firmware,11g,1,4)' 'VR9 PHY11G A1x blob generation missing'
require "$UBOOT_MK" '$(call CompressVR9Firmware,11g,2,2)' 'VR9 PHY11G A2x blob generation missing'
require "$UBOOT_MK" '$(call CompressVR9Firmware,22f,1,4)' 'VR9 PHY22F A1x blob generation missing'
require "$UBOOT_MK" '$(call CompressVR9Firmware,22f,2,2)' 'VR9 PHY22F A2x blob generation missing'
for gphy_fw in \
  "$ROOT/target/linux/lantiq/files/firmware/lantiq/xrx200_phy11g_a14.bin" \
  "$ROOT/target/linux/lantiq/files/firmware/lantiq/xrx200_phy11g_a22.bin" \
  "$ROOT/target/linux/lantiq/files/firmware/lantiq/xrx200_phy22f_a14.bin" \
  "$ROOT/target/linux/lantiq/files/firmware/lantiq/xrx200_phy22f_a22.bin"; do
  [ -s "$gphy_fw" ] || fail "required VR9 GPHY firmware missing: $gphy_fw"
done
pass "SL5501 U-Boot integration"
"$ROOT/scripts/test-sl5501-uboot-patch.sh" >/dev/null || fail "SL5501 U-Boot patch dry-run failed"
pass "SL5501 U-Boot patch dry-run"
[ -f "$UBOOT_DDR_GENERIC" ] || fail "generic VRX200 calibrated DDR patch missing"
[ -f "$UBOOT_SPI_DIAG" ] || fail "SPL SPI-source CRC diagnostic patch missing"
[ -f "$UBOOT_DDR_SL5501" ] || fail "SL5501 trained DDR patch missing"
require "$UBOOT_DDR_SL5501" 'MC_DDR_ECHO_DLL0_TUNED_VALUE 0x28' 'SL5501 trained echo DLL value missing'
require "$UBOOT_DDR_SL5501" 'MC_CCR43_TUNED_VALUE 0x566401' 'SL5501 trained write DQS value missing'
require "$UBOOT_SPI_DIAG" 'SPL: SPI source CRC' 'SPI-vs-DDR CRC diagnostic missing'
sh "$ROOT/scripts/test-sl5501-ddr-calibration.sh" >/dev/null || fail "SL5501 DDR calibration patch regression test failed"
pass "SL5501 trained DDR calibration and SPI/DDR CRC diagnostics"
"$ROOT/scripts/test-sl5501-gphy-prepare.sh" >/dev/null || fail "SL5501 GPHY preparation regression test failed"
pass "SL5501 VR9 GPHY preparation"

# No active inherited MW5/RTL8197F build logic may remain in generic packages.
if grep -Eq 'Tenda Nova MW5|TARGET_realtek_rtl8197f_DEVICE_tenda_nova_mw5' \
  "$ROOT/package/base-files/Makefile" "$ROOT/package/kernel/mac80211/Makefile"; then
  fail "active MW5/RTL8197F build logic remains in generic packages"
fi
require "$ROOT/package/kernel/mac80211/Makefile" '+iwinfo \' 'cfg80211 iwinfo dependency missing'
require "$ROOT/package/kernel/mac80211/Makefile" '+wifi-scripts \' 'cfg80211 wifi-scripts dependency missing'

for stale in \
  "$ROOT/package/kernel/rtl8192cd-rtl8197f" \
  "$ROOT/package/network/config/mw5-base" \
  "$ROOT/package/network/config/rtl8197f-rd05-base" \
  "$ROOT/target/linux/realtek"; do
  [ ! -e "$stale" ] || fail "stale inherited non-SL5501 path remains: $stale"
done
[ "$(grep -c '^TARGET_DEVICES +=' "$VR9MK")" = 1 ] || fail "xrx200 vr9 device list is not SL5501-only"
require "$VR9MK" 'TARGET_DEVICES += sphairon_sl5501' 'SL5501 is not the sole VR9 image target'
LINUX_TARGETS=$(find "$ROOT/target/linux" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null | sort | tr '\n' ' ')
[ "$LINUX_TARGETS" = "generic lantiq " ] || fail "target/linux is not restricted to generic+lantiq: $LINUX_TARGETS"

for sub in ase falcon xrx200_legacy xway xway_legacy; do
  [ ! -e "$ROOT/target/linux/lantiq/$sub" ] || fail "non-SL5501 Lantiq subtarget remains: $sub"
done
pass "SL5501-only project scope"

sh -n "$NET" || fail "02_network shell syntax"
sh -n "$LEDS" || fail "01_leds shell syntax"
sh -n "$UPGRADE" || fail "platform.sh shell syntax"
pass "shell syntax"

# RT3662 bootstrap compile fix + writable default uboot_cfg MAC.
require "$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c" 'int ret;' 'rt3662_resolve_master ret declaration missing'
require "$VR9MK" 'ARTIFACTS := u-boot-sfspl.bin uboot-cfg.bin fullflash-spi.bin' 'standalone uboot_cfg artifact missing'
require "$VR9MK" 'ARTIFACT/uboot-cfg.bin := sl5501-uboot-cfg' 'uboot_cfg artifact recipe missing'
require "$VR9MK" 'seek=524288' 'fullflash does not write uboot_cfg at 0x00080000'
require "$ROOT/scripts/mk-sl5501-uboot-env.py" '1C:74:0D:BA:28:10' 'OEM default MAC missing from uboot_cfg generator'
require "$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c" 'int ret = 0;' 'RT3662 rt3662_resolve_master ret declaration missing'
require "$ROOT/package/kernel/rt3662-inic/Makefile" 'PKG_RELEASE:=23' 'RT3662 package release is not v22 native-persistent heartbeat build'
require "$ROOT/package/boot/uboot-lantiq/patches/0117-MIPS-add-board-support-for-Sphairon-SL5501.patch" '#define CONFIG_ETHADDR          1C:74:0D:BA:28:10' 'compiled U-Boot fallback MAC missing'
sh "$ROOT/scripts/test-sl5501-uboot-env.sh" >/dev/null || fail "SL5501 uboot_cfg env/CRC regression test failed"
pass "RT3662 compile fix and writable OEM-MAC uboot_cfg default"

require "$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c" 'ltq_get_eth_mac()' 'RT3662 does not consume kernel ethaddr identity'
require "$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c" '0x1c, 0x74, 0x0d, 0xba, 0x28, 0x10' 'RT3662 OEM base-MAC fallback missing'
require "$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c" 'MiiMasterMac=%pM' 'OEM MiiMasterMac profile attribute missing'
require "$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c" '__SkipIfNotINIC__MAC=%pM' 'OEM radio-MAC profile attribute missing'
require "$ROOT/package/boot/uboot-lantiq/patches/0117-MIPS-add-board-support-for-Sphairon-SL5501.patch" 'console=ttyLTQ0,115200 ethaddr=${ethaddr}' 'U-Boot does not pass saved ethaddr to Linux'
require "$ROOT/scripts/mk-sl5501-uboot-env.py" 'console=ttyLTQ0,115200 ethaddr=${ethaddr}' 'generated uboot_cfg boot command does not pass ethaddr'
require "$ROOT/target/linux/lantiq/patches-6.6/0035-owrt-lantiq-wifi-and-ethernet-eeprom-handling.patch" 'EXPORT_SYMBOL_GPL(ltq_get_eth_mac)' 'kernel ethaddr accessor is not exported to RT3662 module'
require "$ROOT/target/linux/lantiq/xrx200/base-files/etc/fw_env.config" '/dev/mtd1 0x0 0x2000 0x40000 1' 'static SL5501 fw_env.config missing or wrong'
python3 "$ROOT/scripts/test-sl5501-rt3662-oem-profile.py" >/dev/null || fail "SL5501 OEM RT3662 profile geometry regression test failed"
sh "$ROOT/scripts/test-sl5501-rt3662-transport-v5.sh" >/dev/null || fail "SL5501 RT3662 GSWIP port5 TX-delay diagnostic regression test failed"
sh "$ROOT/scripts/test-sl5501-gswip-v5-patch.sh" >/dev/null || fail "SL5501 GSWIP v5 kernel patch regression test failed"
sh "$ROOT/scripts/test-sl5501-rt3662-rt5392-oem-rgmii-v7.sh" >/dev/null || fail "SL5501 RT3662/RT5392 OEM RGMII v7 regression test failed"
sh "$ROOT/scripts/test-sl5501-rt3662-rt5392-oem-racfg-v8.sh" >/dev/null || fail "SL5501 RT3662/RT5392 OEM RACFG v8 regression test failed"
sh "$ROOT/scripts/test-sl5501-rt3662-rt5392-pcdu5-v9.sh" >/dev/null || fail "SL5501 RT3662/RT5392 PCDU5 v9 regression test failed"
sh "$ROOT/scripts/test-sl5501-rt3662-rt5392-pcdu5-compile-v10.sh" >/dev/null || fail "SL5501 RT3662/RT5392 PCDU5 compile v10 regression test failed"
sh "$ROOT/scripts/test-sl5501-rt3662-rt5392-runtime-diag-v11.sh" >/dev/null || fail "SL5501 RT3662/RT5392 runtime diagnostic v11 regression test failed"
sh "$ROOT/scripts/test-sl5501-rt3662-rt5392-default-wifi-v12.sh" >/dev/null || fail "SL5501 RT3662/RT5392 default Wi-Fi v12 regression test failed"
pass "RT3662 OEM RACFG/bootstrap + corrected VR9 PCDU5 hardware timing integration"

echo "SL5501 static integration validation passed."

"$ROOT/scripts/test-sl5501-rt3662-rt5392-netifd-exec-v13.sh"
"$ROOT/scripts/test-sl5501-rt3662-rt5392-network-config-v14.sh"
"$ROOT/scripts/test-sl5501-rt3662-rt5392-cfg80211-netdev-v15.sh"
"$ROOT/scripts/test-sl5501-rt3662-rt5392-single-start-v16.sh"
"$ROOT/scripts/test-sl5501-rt3662-rt5392-hostapd-nl80211-v17.sh"
"$ROOT/scripts/test-sl5501-rt3662-racfg-ioctl-status-v18.sh"
"$ROOT/scripts/test-sl5501-router-wan-dhcp-wifi-v19.sh"
"$ROOT/scripts/test-sl5501-rt3662-oem-profile-native-v20.sh"
"$ROOT/scripts/test-sl5501-rt3662-station-stability-v21.sh"
"$ROOT/scripts/test-sl5501-rt3662-native-persistent-heartbeat-v22.sh"
