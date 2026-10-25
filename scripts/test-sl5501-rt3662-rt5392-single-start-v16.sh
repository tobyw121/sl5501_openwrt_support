#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
AUTO="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-autostart"
INIT="$ROOT/package/kernel/rt3662-inic/files/etc/init.d/sl5501-wifi"
HOT="$ROOT/package/kernel/rt3662-inic/files/etc/hotplug.d/ieee80211/90-sl5501-wifi-autostart"
SRC="$ROOT/package/kernel/rt3662-inic/src/rt3662_cfg80211.c"
DIAG="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-diag"
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 22'
grep -F 'while [ -e /tmp/.config_pending ]' "$AUTO" >/dev/null || fail 'worker does not wait for OpenWrt base config owner'
grep -F 'OpenWrt base configuration pass complete' "$AUTO" >/dev/null || fail 'base-config completion diagnostic missing'
grep -F "uci -q delete network.lan_dev.ports" "$AUTO" >/dev/null || fail 'canonical bridge-port reset missing'
for p in lan1 lan2 lan3 lan4 wifi; do
	grep -F 'for port in lan1 lan2 lan3 lan4 wifi' "$AUTO" >/dev/null || fail 'canonical SL5501 bridge ports missing'
	break
done
grep -F "ubus call network.wireless up '{}'" "$AUTO" >/dev/null || fail 'direct non-destructive wireless up missing'
! grep -Eq '^[[:space:]]*/sbin/wifi[[:space:]]+up([[:space:]]|$)' "$AUTO" || fail 'destructive /sbin/wifi up remains in worker'
grep -F 'requested|ready)' "$AUTO" >/dev/null || fail 'automatic state guard missing'
grep -F 'cfg80211 AP netdevs present:' "$AUTO" >/dev/null || fail 'sysfs AP-netdev completion check missing'
! grep -Eq '^[[:space:]]*iw[[:space:]]+(phy|dev)([[:space:]]|$)' "$AUTO" || fail 'worker still performs blocking iw dump'
grep -F 'del_virtual_intf request name=%s' "$SRC" >/dev/null || fail 'VIF teardown diagnostic missing'
grep -F 'AP netdev removal completed' "$SRC" >/dev/null || fail 'VIF teardown completion diagnostic missing'
grep -F 'unregister_netdevice: waiting' "$DIAG" >/dev/null || fail 'netdev lifecycle diagnostic missing'
grep -F 'sl5501-wifi-autostart init' "$INIT" >/dev/null || fail 'init fallback missing'
grep -F 'sl5501-wifi-autostart hotplug' "$HOT" >/dev/null || fail 'hotplug primary trigger missing'
sh -n "$AUTO" || fail 'autostart shell syntax invalid'
sh -n "$INIT" || fail 'init shell syntax invalid'
sh -n "$HOT" || fail 'hotplug shell syntax invalid'
sh -n "$DIAG" || fail 'diagnostic shell syntax invalid'
grep -F "uci -q delete wireless.default_radio0.network" "$AUTO" >/dev/null || fail 'radio0 synthetic bridge detachment missing'
grep -F "uci -q delete wireless.default_radio1.network" "$AUTO" >/dev/null || fail 'radio1 synthetic bridge detachment missing'

echo 'OK: v16 serializes first boot and uses one non-destructive wireless-up path'
