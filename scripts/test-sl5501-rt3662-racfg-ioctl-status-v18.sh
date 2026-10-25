#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
P0="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat"
P1="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat"
WIFI="$ROOT/package/kernel/rt3662-inic/files/etc/config/wireless"
fail() { echo "FAIL: $*" >&2; exit 1; }

check_pair() {
	profile="$1"
	ssid="$2"
	grep -F "SSID1=$ssid" "$profile" >/dev/null || fail "$profile SSID does not match OpenWrt UCI"
	grep -F 'AuthMode=WPA2PSK' "$profile" >/dev/null || fail "$profile is not WPA2-PSK"
	grep -F 'EncrypType=AES' "$profile" >/dev/null || fail "$profile is not AES/CCMP"
	grep -F 'WPAPSK1=12345678!' "$profile" >/dev/null || fail "$profile PSK differs from OpenWrt UCI"
	grep -F 'WirelessEvent=1' "$profile" >/dev/null || fail "$profile async wireless events are disabled"
	grep -F 'WmmCapable=1' "$profile" >/dev/null || fail "$profile WMM is disabled"
}

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 22'
grep -F 'got == RT3662_CMD_TYPE_IOCTL_STATUS' "$RACFG" >/dev/null || fail 'runtime IOCTL_STATUS completion is missing'
grep -F 'cmd_seq == inic->sync.cmd_seq' "$RACFG" >/dev/null || fail 'runtime command_seq matching is missing'
grep -F 'got == expected || got == RT3662_CMD_TYPE_COPY_TO_USER' "$RACFG" >/dev/null || fail 'intermediate IWREQ/IW_HANDLER response handling is missing'
grep -F 'dev_type and' "$RACFG" >/dev/null || fail 'OEM runtime matcher rationale is missing'
grep -F 'runtime RACFG timeout:' "$RACFG" >/dev/null || fail 'runtime timeout diagnostic is missing'
grep -F "radio%u private SET '%s' failed" "$RACFG" >/dev/null || fail 'private SET diagnostic is missing'
check_pair "$P0" 'openwrt_5GHz'
check_pair "$P1" 'openwrt_2,4GHz'
grep -F "option ssid 'openwrt_5GHz'" "$WIFI" >/dev/null || fail '5 GHz UCI SSID mismatch'
grep -F "option ssid 'openwrt_2,4GHz'" "$WIFI" >/dev/null || fail '2.4 GHz UCI SSID mismatch'
[ "$(grep -c "option key '12345678!'" "$WIFI")" -eq 2 ] || fail 'UCI PSK mismatch'

echo 'OK: v18 accepts OEM runtime IOCTL_STATUS completion and aligns firmware SSID/WPA2 profiles with UCI'
