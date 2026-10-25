#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
H="$ROOT/package/kernel/rt3662-inic/src/rt3662_inic.h"
R="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
M="$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c"
B="$ROOT/package/base-files/files/bin/board_detect"
P0="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat"
P1="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat"

grep -q 'RT3662_BOOT_RETRIES 10' "$H"
grep -q 'RT3662_BOOT_TIMEOUT_MS 1000' "$H"
grep -F '((u8 *)&hdr->flags)[0] = 0x00;' "$R" >/dev/null
grep -F '((u8 *)&hdr->flags)[1] = RT3662_FLAG_BIG_ENDIAN;' "$R" >/dev/null
! grep -q 'hdr->flags = cpu_to_le16(RT3662_FLAG_BIG_ENDIAN);' "$R"
grep -q 'expected == RT3662_CMD_TYPE_BOOTSTRAP' "$R"
grep -q 'bootstrap ACK timeout' "$R"
grep -q 'OEM INITCFG stream:' "$R"
grep -q '__SkipIfNotINIC__MAC=%pM' "$R"
grep -q 'MiiMasterMac=%pM' "$R"
grep -q 'OEM bootstrap RX: type=%04x id=%04x seq=%u len=%zu' "$R"
grep -q 'bootstrap echo mismatch' "$R"
grep -q 'dev_set_promiscuity(inic->master, 1)' "$M"
grep -q 'dev_set_allmulti(inic->master, 1)' "$M"
grep -q 'dev_mc_add(inic->master, rt3662_boot_host_mc)' "$M"
[ "$(wc -c < "$P0")" -eq 2311 ]
[ "$(wc -c < "$P1")" -eq 2213 ]
[ -f "$B" ]
grep -q 'chmod 0755 $(1)/bin/board_detect' "$ROOT/package/base-files/Makefile"
grep -q '/etc/board.d/' "$B"

echo 'OK: RT3662 OEM 5-packet INITCFG/bootstrap transport, multicast RX and board_detect'
