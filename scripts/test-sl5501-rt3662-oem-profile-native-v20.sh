#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_cfg80211.c"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
HDR="$ROOT/package/kernel/rt3662-inic/src/rt3662_inic.h"
OEM="$ROOT/package/kernel/rt3662-inic/oem-reference/ralink-2.4.0.9.10/comm/raconfig.c"
P5="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat"
P2="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat"
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -F 'SetRadioOn(pAd, 1);' "$OEM" >/dev/null || fail 'OEM post-GET_MAC RadioOn reference missing'
grep -F 'OEM post-GET_MAC RadioOn=1 complete; profile beacon enabled' "$CFG" >/dev/null || fail 'fw_ready does not turn profile radios on'
grep -F 'AP started from uploaded OEM profile' "$CFG" >/dev/null || fail 'profile-native START_AP fast path missing'
grep -F 'bool profile_managed_ap;' "$HDR" >/dev/null || fail 'profile-managed AP state missing'
grep -F 'firmware owns WPA key installation' "$CFG" >/dev/null || fail 'firmware-native WPA key ownership missing'
grep -F 'firmware-native station %pM %s' "$CFG" >/dev/null || fail 'duplicate hostapd station SME suppression missing'
grep -F 'radio->profile_managed_ap || radio->native_profile_active' "$RACFG" >/dev/null || fail 'firmware-native EAPOL RX suppression missing'

[ "$(wc -c < "$P5")" -eq 2311 ] || fail '5 GHz profile geometry changed'
[ "$(wc -c < "$P2")" -eq 2213 ] || fail '2.4 GHz profile geometry changed'
grep -Fx 'SSID1=openwrt_5GHz' "$P5" >/dev/null || fail '5 GHz SSID mismatch'
grep -Fx 'Channel=40' "$P5" >/dev/null || fail '5 GHz channel mismatch'
grep -Fx 'SSID1=openwrt_2,4GHz' "$P2" >/dev/null || fail '2.4 GHz SSID mismatch'
grep -Fx 'Channel=1' "$P2" >/dev/null || fail '2.4 GHz channel mismatch'
for p in "$P5" "$P2"; do
    grep -Fx 'CountryCode=DE' "$p" >/dev/null || fail "$p CountryCode mismatch"
    grep -Fx 'AuthMode=WPA2PSK' "$p" >/dev/null || fail "$p AuthMode mismatch"
    grep -Fx 'EncrypType=AES' "$p" >/dev/null || fail "$p cipher mismatch"
    grep -Fx 'WPAPSK1=12345678!' "$p" >/dev/null || fail "$p PSK mismatch"
    grep -Fx 'WirelessEvent=1' "$p" >/dev/null || fail "$p WirelessEvent mismatch"
done

python3 - "$CFG" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
start=s.index('static int rt3662_start_ap(')
stop=s.index('static int rt3662_stop_ap(', start)
body=s[start:stop]
fast=body.index('if (rt3662_profile_matches_ap(radio, settings))')
legacy=body.index('radio->profile_managed_ap = false;', fast)
branch=body[fast:legacy]
for forbidden in ('rt3662_set_essid(', 'rt3662_set_freq(', 'rt3662_config_security(', 'rt3662_set_genie(', 'rt3662_iw_private_set('):
    if forbidden in branch:
        raise SystemExit('profile fast path still executes '+forbidden)
if 'return 0;' not in branch:
    raise SystemExit('profile fast path does not complete START_AP')
print('OK: profile-native branch skips all redundant WEXT/beacon/security replay')
PY

echo 'PASS: SL5501 v20 OEM profile-native beacon/WPA ownership'
