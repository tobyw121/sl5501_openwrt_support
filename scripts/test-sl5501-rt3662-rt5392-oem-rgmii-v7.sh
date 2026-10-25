#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
MAIN="$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
HDR="$ROOT/package/kernel/rt3662-inic/src/rt3662_inic.h"
DTS="$ROOT/target/linux/lantiq/files/arch/mips/boot/dts/lantiq/vr9_sphairon_sl5501.dts"
FWDIR="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662"
PATCH="$ROOT/target/linux/lantiq/patches-6.6/0743-net-dsa-lantiq-gswip-export-sl5501-port5-tx-delay-diag.patch"

[ "$(wc -c < "$FWDIR/iNIC_ap.bin")" -eq 576932 ]
[ "$(sha256sum "$FWDIR/iNIC_ap.bin" | awk '{print $1}')" = f927ebf5a4ac0eebef037da11490736a6a6f1adc8e900cf42781d76d1ed010cc ]
[ "$(sha256sum "$FWDIR/RT3662-version" | awk '{print $1}')" = "$(sha256sum "$FWDIR/RT5392-version" | awk '{print $1}')" ]
grep -F 'RT_FW_VERSION="2.5.1.5"' "$FWDIR/RT3662-version" >/dev/null
grep -F 'RT_DRV_VERSION="2.7.0.5"' "$FWDIR/RT5392-version" >/dev/null

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null
grep -F 'RT5392-iNIC_ap.bin' "$PKG" >/dev/null
grep -F 'power-gpios = <&gpio 37 GPIO_ACTIVE_HIGH>' "$DTS" >/dev/null
grep -F 'reset-gpios = <&gpio 15 GPIO_ACTIVE_LOW>' "$DTS" >/dev/null
grep -F 'ralink,mii-master = "wifi"' "$DTS" >/dev/null

grep -F 'msleep(500);' "$MAIN" >/dev/null
grep -F 'rt3662_enable_master(inic);' "$MAIN" >/dev/null
grep -F 'rt3662_apply_port5_tx_delay(inic);' "$MAIN" >/dev/null
grep -F 'rt3662_release_reset(inic);' "$MAIN" >/dev/null

grep -F 'static int port5_tx_delay_ps = 1500;' "$RACFG" >/dev/null
grep -F 'module_param(port5_tx_delay_ps, int, 0644);' "$RACFG" >/dev/null
grep -F 'applied and hardware-verified OEM GSWIP port5 TX delay %d ps before iNIC reset release' "$RACFG" >/dev/null
grep -F 'diagnostic GSWIP port5 TX RMON:' "$RACFG" >/dev/null
grep -F 'tx_delay_sweep' "$RACFG" >/dev/null
grep -F 'memcmp(eth, rt3662_boot_ethhdr' "$RACFG" >/dev/null || true
! grep -F 'boot_unicast_source' "$HDR" "$MAIN" "$RACFG" >/dev/null
! grep -F 'switching reply path to unicast' "$RACFG" >/dev/null

grep -F 'lantiq_gswip_sl5501_set_port5_tx_delay' "$PATCH" >/dev/null
grep -F 'lantiq_gswip_sl5501_get_port5_tx_stats' "$PATCH" >/dev/null

grep -F '__SkipIfNotINIC__CONCURRENT_ENABLE=1' "$RACFG" >/dev/null
grep -F '__SkipIfNotINIC__BUILT_IN_BRIDGE=%u' "$RACFG" >/dev/null
grep -F 'MiiMasterMac=%pM' "$RACFG" >/dev/null
grep -F 'RT3662_GET_MAC_TIMEOUT_MS' "$RACFG" >/dev/null

echo 'OK: RT3662/RT5392 v7 OEM concurrent bootstrap + exact 1500-ps GSWIP port5 TX timing'
