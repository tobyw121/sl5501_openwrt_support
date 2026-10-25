#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
DIAG="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-diag"

fail() { echo "FAIL: $*" >&2; exit 1; }

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'package release is not 22'
grep -F 'sl5501-wifi-diag' "$PKG" >/dev/null || fail 'runtime diagnostic is not installed'
[ -x "$DIAG" ] || fail 'runtime diagnostic is missing or not executable'
grep -F 'OEM bootstrap RX: type=%04x id=%04x seq=%u len=%zu' "$RACFG" >/dev/null || fail 'stable bootstrap RX log missing'
! grep -F 'bootstrap RX: %pM -> %pM' "$RACFG" >/dev/null || fail 'misleading boot-ROM L2 diagnostic still active'
grep -F 'OEM bootstrap upload complete: cmd=%u packets=%u' "$RACFG" >/dev/null || fail 'upload completion marker missing'
grep -F 'OEM BOOT_STARTUP sent; waiting up to %u ms for spontaneous concurrent GET_MAC' "$RACFG" >/dev/null || fail 'STARTUP/GET_MAC marker missing'
grep -F 'OEM GET_MAC received after %u ms, payload=%zu bytes' "$RACFG" >/dev/null || fail 'GET_MAC latency marker missing'
grep -F "section cfg80211" "$DIAG" >/dev/null || fail 'cfg80211 diagnostic section missing'
grep -F "ubus call network.wireless status" "$DIAG" >/dev/null || fail 'netifd diagnostic missing'
grep -F "bridge link" "$DIAG" >/dev/null || fail 'bridge diagnostic missing'
sh -n "$DIAG" || fail 'runtime diagnostic shell syntax invalid'

echo 'OK: SL5501 v11 preserves working OEM bootstrap and adds runtime cfg80211/hostapd diagnostics'
