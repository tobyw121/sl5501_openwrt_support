#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
WIFI="$ROOT/package/kernel/rt3662-inic/files/etc/config/wireless"
HOTPLUG="$ROOT/package/kernel/rt3662-inic/files/etc/hotplug.d/ieee80211/90-sl5501-wifi-autostart"
WORKER="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-autostart"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -f "$WIFI" ] || fail 'default /etc/config/wireless source missing'
[ -x "$HOTPLUG" ] || fail 'SL5501 Wi-Fi autostart hotplug helper missing or not executable'
[ -x "$WORKER" ] || fail 'SL5501 Wi-Fi autostart worker missing or not executable'
grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'package release is not 22'
grep -F 'KernelPackage/rt3662-inic/conffiles' "$PKG" >/dev/null || fail 'wireless config is not registered as a conffile'
grep -F '/etc/config/wireless' "$PKG" >/dev/null || fail 'wireless config not installed/preserved'
grep -F '90-sl5501-wifi-autostart' "$PKG" >/dev/null || fail 'autostart hotplug helper not installed'

# 5 GHz: OEM radio0 / phy0.
grep -F "config wifi-device 'radio0'" "$WIFI" >/dev/null || fail '5 GHz radio0 section missing'
grep -F "option phy 'phy0'" "$WIFI" >/dev/null || fail '5 GHz phy0 binding missing'
grep -F "option band '5g'" "$WIFI" >/dev/null || fail '5 GHz band missing'
grep -F "option channel '40'" "$WIFI" >/dev/null || fail '5 GHz channel 40 missing'
grep -F "option ssid 'openwrt_5GHz'" "$WIFI" >/dev/null || fail '5 GHz SSID missing'

# 2.4 GHz: concurrent RT5392 radio1 / phy1.
grep -F "config wifi-device 'radio1'" "$WIFI" >/dev/null || fail '2.4 GHz radio1 section missing'
grep -F "option phy 'phy1'" "$WIFI" >/dev/null || fail '2.4 GHz phy1 binding missing'
grep -F "option band '2g'" "$WIFI" >/dev/null || fail '2.4 GHz band missing'
grep -F "option channel '1'" "$WIFI" >/dev/null || fail '2.4 GHz channel 1 missing'
grep -F "option ssid 'openwrt_2,4GHz'" "$WIFI" >/dev/null || fail '2.4 GHz SSID missing'

[ "$(grep -c "option disabled '0'" "$WIFI")" -eq 2 ] || fail 'both radios are not enabled by default'
[ "$(grep -c "option encryption 'psk2'" "$WIFI")" -eq 2 ] || fail 'both APs are not WPA2-PSK'
[ "$(grep -c "option key '12345678!'" "$WIFI")" -eq 2 ] || fail 'both APs do not share the requested PSK'
[ "$(grep -c "option network 'lan'" "$WIFI" || true)" -eq 0 ] || fail 'synthetic AP facades must not be bridge payload ports'

# Avoid a phy-registration race: reload only after the iNIC runtime-ready marker.
grep -F "RT3662 iNIC firmware running:" "$WORKER" >/dev/null || fail 'firmware-ready wait missing'
grep -F '/sbin/wifi up' "$WORKER" >/dev/null || fail 'netifd Wi-Fi bring-up missing'
sh -n "$HOTPLUG" || fail 'autostart hotplug shell syntax invalid'
sh -n "$WORKER" || fail 'autostart worker shell syntax invalid'

echo 'OK: SL5501 v12 enables RT3662 5 GHz + RT5392 2.4 GHz APs by default with shared WPA2 PSK'
