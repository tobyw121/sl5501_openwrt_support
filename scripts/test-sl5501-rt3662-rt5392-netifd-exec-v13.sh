#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
fail() { echo "FAIL: $*" >&2; exit 1; }
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
WIFI_MK="$ROOT/package/network/config/wifi-scripts/Makefile"
VR9="$ROOT/target/linux/lantiq/image/vr9.mk"
DEF="$ROOT/configs/sl5501_defconfig"
WORKER="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-autostart"
DIAG="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-diag"

for f in "$PKG" "$WIFI_MK" "$VR9" "$DEF" "$WORKER" "$DIAG"; do [ -f "$f" ] || fail "missing $f"; done
grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 22'
grep -F 'PKG_RELEASE:=2' "$WIFI_MK" >/dev/null || fail 'wifi-scripts release != 2'
grep -F 'chmod 0755 $(1)/sbin/wifi' "$WIFI_MK" >/dev/null || fail '/sbin/wifi mode not forced'
grep -F 'chmod 0755 $(1)/lib/netifd/wireless/mac80211.sh' "$WIFI_MK" >/dev/null || fail 'mac80211 backend mode not forced'
grep -F 'kmod-rt3662-inic wifi-scripts iwinfo wpad-basic-mbedtls' "$VR9" >/dev/null || fail 'device image lacks explicit wireless userspace'
grep -F 'CONFIG_PACKAGE_wifi-scripts=y' "$DEF" >/dev/null || fail 'defconfig lacks wifi-scripts'
grep -F 'CONFIG_PACKAGE_iwinfo=y' "$DEF" >/dev/null || fail 'defconfig lacks iwinfo'
grep -F 'RT3662 iNIC firmware running:' "$WORKER" >/dev/null || fail 'worker does not wait for firmware'
grep -F '/sbin/wifi up' "$WORKER" >/dev/null || fail 'worker does not invoke wifi up'
grep -F 'network.wireless status' "$WORKER" >/dev/null || fail 'worker does not verify netifd state'
grep -F 'netifd-hostapd-log' "$DIAG" >/dev/null || fail 'diag lacks userspace logs'
[ -x "$ROOT/package/network/config/wifi-scripts/files/sbin/wifi" ] || fail 'source /sbin/wifi is not executable'
[ -x "$ROOT/package/network/config/wifi-scripts/files/lib/netifd/wireless/mac80211.sh" ] || fail 'source mac80211.sh is not executable'
[ -x "$WORKER" ] || fail 'autostart worker is not executable'
sh -n "$WORKER"
sh -n "$ROOT/package/kernel/rt3662-inic/files/etc/init.d/sl5501-wifi"
sh -n "$ROOT/package/kernel/rt3662-inic/files/etc/hotplug.d/ieee80211/90-sl5501-wifi-autostart"
echo 'OK: v13 forces executable OpenWrt Wi-Fi backend and verifies dual-radio netifd bring-up'
