#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
SRC="$ROOT/package/kernel/rt3662-inic/src/rt3662_cfg80211.c"
P0="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat"
P1="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat"
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 22'
grep -F 'static int rt3662_connect_unsupported(' "$SRC" >/dev/null || fail 'hostapd CONNECT compatibility callback missing'
grep -F 'static int rt3662_disconnect_unsupported(' "$SRC" >/dev/null || fail 'paired DISCONNECT callback missing'
grep -F '.connect = rt3662_connect_unsupported,' "$SRC" >/dev/null || fail 'CONNECT callback not registered'
grep -F '.disconnect = rt3662_disconnect_unsupported,' "$SRC" >/dev/null || fail 'DISCONNECT callback not registered'
grep -F 'return -EOPNOTSUPP;' "$SRC" >/dev/null || fail 'AP-only station callback rejection missing'
grep -F 'wiphy->interface_modes = BIT(NL80211_IFTYPE_AP);' "$SRC" >/dev/null || fail 'wiphy is no longer AP-only'
! grep -Eq 'interface_modes.*NL80211_IFTYPE_STATION' "$SRC" || fail 'station mode was accidentally advertised'
grep -F 'WIPHY_FLAG_HAVE_AP_SME' "$SRC" >/dev/null || fail 'firmware AP SME flag missing'
grep -F 'crypto->akm_suites[0] == WLAN_AKM_SUITE_8021X' "$SRC" >/dev/null || fail 'IEEE8021X is not tied to enterprise AKM'
grep -F 'IEEE8021X=0' "$P0" >/dev/null || fail 'OEM 5 GHz profile does not confirm PSK IEEE8021X=0'
grep -F 'IEEE8021X=0' "$P1" >/dev/null || fail 'OEM 2.4 GHz profile does not confirm PSK IEEE8021X=0'
grep -F '.start_ap = rt3662_start_ap,' "$SRC" >/dev/null || fail 'START_AP callback missing'
grep -F '.add_key = rt3662_add_key,' "$SRC" >/dev/null || fail 'key install callback missing'

echo 'OK: v17 advertises hostapd-compatible nl80211 CONNECT without enabling STA mode and fixes PSK IEEE8021X mapping'
