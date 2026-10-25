#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
CFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_cfg80211.c"
WIFI="$ROOT/package/kernel/rt3662-inic/files/etc/config/wireless"
AUTO="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-autostart"
P5="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat"
P2="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat"
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 23'
[ "$(wc -c < "$P5")" -eq 2311 ] || fail '5 GHz profile geometry changed'
[ "$(wc -c < "$P2")" -eq 2213 ] || fail '2.4 GHz profile geometry changed'

# v22 deliberately reverts every speculative v21 profile-timer edit.  The
# production profiles are again byte-exact to v20; keep only the host-side
# diagnostics/guards that are independent of firmware profile semantics.
for p in "$P5" "$P2"; do
    grep -Fx 'RekeyInterval=0' "$p" >/dev/null || fail "$p WPA rekey interval changed"
    grep -Fx 'RekeyMethod=DISABLE' "$p" >/dev/null || fail "$p WPA rekey method changed"
    grep -Fx 'session_timeout_interval=3600' "$p" >/dev/null || fail "$p OEM session timeout not restored"
    ! grep -q '^StationKeepAlive=' "$p" || fail "$p contains speculative StationKeepAlive override"
    ! grep -q '^DLSCapable=' "$p" || fail "$p contains speculative DLS override"
    ! grep -q '^idle_timeout_interval=' "$p" || fail "$p contains speculative idle-timeout override"
done
grep -Fx 'IEEE80211H=1' "$P5" >/dev/null || fail 'v20/OEM IEEE80211H value not restored'
grep -Fx 'CSPeriod=10' "$P5" >/dev/null || fail 'v20/OEM CSPeriod value not restored'
grep -Fx 'FastDfs=1' "$P5" >/dev/null || fail 'v20/OEM FastDfs value not restored'
grep -Fx 'ChMovTime=60' "$P5" >/dev/null || fail 'v20/OEM ChMovTime value not restored'

[ "$(grep -c "option disassoc_low_ack '0'" "$WIFI")" -eq 2 ] || fail 'hostapd low-ACK guard missing'
[ "$(grep -c "option max_inactivity '86400'" "$WIFI")" -eq 2 ] || fail 'hostapd inactivity guard missing'
[ "$(grep -c "disassoc_low_ack='0'" "$AUTO")" -eq 2 ] || fail 'autostart low-ACK guard missing'
[ "$(grep -c "max_inactivity='86400'" "$AUTO")" -eq 2 ] || fail 'autostart inactivity guard missing'

grep -F 'radio%u firmware-native station %pM %s' "$CFG" >/dev/null || fail 'firmware-native station diagnostics missing'
grep -F 'assoc ? "associated" : "disassociated"' "$CFG" >/dev/null || fail 'station event direction diagnostics missing'

echo 'PASS: SL5501 v21 hostapd/station diagnostics retained; production profiles fully restored to v20 in v22'
