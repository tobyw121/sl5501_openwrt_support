#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SRC="$ROOT/package/kernel/rt3662-inic/src/rt3662_cfg80211.c"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
DIAG="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-diag"
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 22'
grep -F 'ret = cfg80211_register_netdevice(ndev);' "$SRC" >/dev/null || fail 'cfg80211-aware netdev registration missing'
grep -F 'cfg80211_unregister_netdevice(radio->ndev);' "$SRC" >/dev/null || fail 'cfg80211-aware netdev unregistration missing'
! grep -F 'ret = register_netdevice(ndev);' "$SRC" >/dev/null || fail 'deadlocking plain register_netdevice remains in add_virtual_intf'
! grep -Eq '^[[:space:]]*unregister_netdevice\(radio->ndev\);' "$SRC" >/dev/null || fail 'plain unregister_netdevice remains in del_virtual_intf'
grep -F 'nl80211 full dumps intentionally skipped' "$DIAG" >/dev/null || fail 'safe diagnostic explanation missing'
! grep -Eq '^[[:space:]]*iw[[:space:]]+(phy|dev)([[:space:]]|$)' "$DIAG" || fail 'default diagnostic still runs blocking nl80211 dump'
sh -n "$DIAG" || fail 'diagnostic shell syntax invalid'

echo 'OK: v15 uses cfg80211 netdev helpers and default diagnostics avoid blocking wiphy dumps'
