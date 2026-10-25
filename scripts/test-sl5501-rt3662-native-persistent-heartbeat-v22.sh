#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
CFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_cfg80211.c"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
HDR="$ROOT/package/kernel/rt3662-inic/src/rt3662_inic.h"
MAIN="$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c"
P5="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat"
P2="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat"
OEM="$ROOT/package/kernel/rt3662-inic/oem-reference/ralink-2.4.0.9.10/comm/raconfig.c"
OEMH="$ROOT/package/kernel/rt3662-inic/oem-reference/ralink-2.4.0.9.10/comm/raconfig.h"
fail(){ echo "FAIL: $*" >&2; exit 1; }

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 23'
grep -F 'bool native_profile_active;' "$HDR" >/dev/null || fail 'persistent profile-native state missing'

# The cfg80211 AP netdev is only a facade. A hostapd/netifd teardown must not
# switch off the real profile-owned full-MAC AP.
grep -F 'profile-native STOP_AP: preserving firmware RadioOn=1' "$CFG" >/dev/null || fail 'STOP_AP persistence guard missing'
grep -F 'profile-native del_virtual_intf: preserving firmware RadioOn=1' "$CFG" >/dev/null || fail 'del_virtual_intf persistence guard missing'
grep -F '!radio->native_profile_active' "$CFG" >/dev/null || fail 'dynamic RadioOn=0 path no longer separated from native mode'
[ "$(grep -F 'RadioOn=0' "$CFG" | wc -l)" -ge 2 ] || fail 'dynamic RadioOn=0 support was removed globally'

# Native ownership must remain true after the synthetic facade was stopped,
# including EAPOL and station-event routing.
[ "$(grep -F 'radio->profile_managed_ap || radio->native_profile_active' "$CFG" | wc -l)" -ge 5 ] || fail 'cfg80211 native ownership guards incomplete'
grep -F 'radio->profile_managed_ap || radio->native_profile_active' "$RACFG" >/dev/null || fail 'RX EAPOL native ownership guard missing'

# OEM reference: heartbeat command 0x0004, 10-second timer and restart when no
# heartbeat was counted. The new implementation watches that same feedback.
grep -F '#define RACFG_CMD_HEART_BEAT' "$OEMH" >/dev/null || fail 'OEM heartbeat command reference missing'
grep -F '#define HEART_BEAT_TIMEOUT    10' "$OEMH" >/dev/null || fail 'OEM heartbeat interval reference missing'
grep -F 'if (pAd->RaCfgObj.HeartBeatCount == 0)' "$OEM" >/dev/null || fail 'OEM heartbeat restart reference missing'

grep -F '#define RT3662_HEARTBEAT_CHECK_SEC 10' "$RACFG" >/dev/null || fail '10-second heartbeat check missing'
grep -F '#define RT3662_HEARTBEAT_STALE_SEC 20' "$RACFG" >/dev/null || fail 'heartbeat stale threshold missing'
grep -F '#define RT3662_HEARTBEAT_DISCOVERY_SEC 30' "$RACFG" >/dev/null || fail 'heartbeat discovery grace missing'
grep -F 'OEM RACFG heartbeat stream detected' "$RACFG" >/dev/null || fail 'heartbeat RX detection missing'
grep -F 'watchdog remains passive' "$RACFG" >/dev/null || fail 'passive-until-observed heartbeat safety missing'
grep -F 'OEM RACFG heartbeat stale for >%u s; performing OEM-style iNIC GPIO recovery' "$RACFG" >/dev/null || fail 'heartbeat recovery missing'
grep -F 'rt3662_restart_inic_hw(inic)' "$RACFG" >/dev/null || fail 'heartbeat GPIO restart hook missing'
grep -F 'rt3662_heartbeat_fw_ready(inic);' "$CFG" >/dev/null || fail 'heartbeat monitor not armed after firmware ready'
grep -F 'rt3662_heartbeat_stop(inic);' "$MAIN" >/dev/null || fail 'heartbeat work not cancelled on remove'
grep -F 'INIT_DELAYED_WORK(&inic->heartbeat_work, rt3662_heartbeat_worker);' "$RACFG" >/dev/null || fail 'heartbeat delayed work not initialized'

# Never reset merely because this firmware revision has not emitted heartbeat.
grep -F 'if (!READ_ONCE(inic->heartbeat_seen))' "$RACFG" >/dev/null || fail 'first-heartbeat gate missing'

# Profile transport geometry remains the proven OEM bootstrap geometry.
[ "$(wc -c < "$P5")" -eq 2311 ] || fail '5 GHz profile geometry changed'
[ "$(wc -c < "$P2")" -eq 2213 ] || fail '2.4 GHz profile geometry changed'
python3 "$ROOT/scripts/test-sl5501-rt3662-oem-profile.py" >/dev/null || fail '4739-byte INITCFG geometry failed'
for p in "$P5" "$P2"; do
    grep -Fx 'RekeyInterval=0' "$p" >/dev/null || fail "$p rekey interval changed"
    grep -Fx 'RekeyMethod=DISABLE' "$p" >/dev/null || fail "$p rekey method changed"
    grep -Fx 'session_timeout_interval=3600' "$p" >/dev/null || fail "$p OEM session timeout not restored"
done
grep -Fx 'IEEE80211H=1' "$P5" >/dev/null || fail 'byte-exact v20 5 GHz profile not restored'
grep -Fx 'ChMovTime=60' "$P5" >/dev/null || fail 'byte-exact v20 5 GHz profile not restored'

echo 'PASS: SL5501 v22 persistent firmware-native AP + OEM RACFG heartbeat watchdog'
