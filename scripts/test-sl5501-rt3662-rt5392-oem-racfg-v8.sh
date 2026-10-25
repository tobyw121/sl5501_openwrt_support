#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
MAIN="$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
HDR="$ROOT/package/kernel/rt3662-inic/src/rt3662_inic.h"
FWDIR="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662"
OEM="$ROOT/package/kernel/rt3662-inic/oem-reference/ralink-2.4.0.9.10/comm/raconfig.c"

# Current package + exact SL5501 firmware/profile geometry.
grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null
[ "$(wc -c < "$FWDIR/iNIC_ap.bin")" -eq 576932 ]
[ "$(sha256sum "$FWDIR/iNIC_ap.bin" | awk '{print $1}')" = f927ebf5a4ac0eebef037da11490736a6a6f1adc8e900cf42781d76d1ed010cc ]
[ "$(wc -c < "$FWDIR/iNIC_ap.dat")" -eq 2311 ]
[ "$(wc -c < "$FWDIR/iNIC_ap1.dat")" -eq 2213 ]
[ "$(wc -c < "$FWDIR/iNIC_e2p.bin")" -eq 1024 ]
[ "$(wc -c < "$FWDIR/iNIC_e2p1.bin")" -eq 512 ]

# OEM upload_timeout(): first TX plus RetryCount=10 retransmissions.
grep -F '#define RT3662_BOOT_RETRIES 10' "$HDR" >/dev/null
grep -F '#define RT3662_BOOT_TX_ATTEMPTS (RT3662_BOOT_RETRIES + 1)' "$HDR" >/dev/null
grep -F 'attempt < RT3662_BOOT_TX_ATTEMPTS' "$RACFG" >/dev/null
grep -F 'initial + %u retries' "$RACFG" >/dev/null

# Exact SendRaCfgCommand MII ABI and OEM bootstrap ACK rules.
grep -F 'skb_reserve(skb, NET_SKB_PAD + 2);' "$RACFG" >/dev/null
grep -F 'memcpy(eth, rt3662_boot_ethhdr, ETH_HLEN);' "$RACFG" >/dev/null
grep -F '((u8 *)&hdr->flags)[0] = 0x00;' "$RACFG" >/dev/null
grep -F '((u8 *)&hdr->flags)[1] = RT3662_FLAG_BIG_ENDIAN;' "$RACFG" >/dev/null
grep -F 'OEM bootstrap ACK actions ignore state/status completely.' "$RACFG" >/dev/null
grep -F 'inic->sync.status = 0;' "$RACFG" >/dev/null
grep -F 'memcmp(echo, data, chunk)' "$RACFG" >/dev/null
grep -F 'return -EBADMSG;' "$RACFG" >/dev/null

# RaCfgSetUp()/RaCfgWaitSyncRsp: GET_MAC is spontaneous.  The waiter must be
# armed before BOOT_STARTUP and cmd_seq must be zero; no active GET_MAC xfer.
grep -F 'static int rt3662_startup_wait_get_mac' "$RACFG" >/dev/null
grep -F 'inic->sync.cmd_id = RT3662_CMD_GET_MAC;' "$RACFG" >/dev/null
grep -F 'inic->sync.cmd_seq = 0;' "$RACFG" >/dev/null
grep -F 'RT3662_CMD_BOOT_STARTUP, 0, 0, 0, 0' "$RACFG" >/dev/null
grep -F 'msecs_to_jiffies(RT3662_GET_MAC_TIMEOUT_MS)' "$RACFG" >/dev/null
grep -F 'reply_len != 35' "$RACFG" >/dev/null
grep -F 'reply + 17' "$RACFG" >/dev/null
! grep -F 'rt3662_racfg_xfer(inic, 0, RT3662_CMD_TYPE_SYNC,' "$RACFG" >/dev/null

# Sync responses require RSP_FLAG; concurrent GET_MAC deliberately ignores
# dev_type exactly like OEM DispatchAdapter().
grep -F '(type & RT3662_CMD_TYPE_RSP_FLAG)' "$RACFG" >/dev/null
grep -F 'inic->sync.armed && inic->sync.cmd_id == id' "$RACFG" >/dev/null
grep -F 'cmd_seq == inic->sync.cmd_seq' "$RACFG" >/dev/null
grep -F 'OEM DispatchAdapter() deliberately' "$RACFG" >/dev/null

# The SL5501 2.7.0.5 binary advances immediately on a valid firmware echo.
! grep -F 'msleep(30);' "$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c" >/dev/null

# RaCfgRestartiNIC MII recovery: reset pulse, keep power/P5 alive, restore PCDU5.
grep -F 'int rt3662_restart_inic_hw' "$MAIN" >/dev/null
grep -F 'usleep_range(10000, 12000);' "$MAIN" >/dev/null
grep -F 'rt3662_apply_port5_tx_delay(inic);' "$MAIN" >/dev/null
grep -F 'Never strand the radio in reset' "$MAIN" >/dev/null
grep -F 'restart_hw = true;' "$RACFG" >/dev/null

# After GET_MAC, syncmiimac=0 uses target MAC -> MiiMasterMac filtering.
grep -F 'OEM MII syncmiimac=0 L2 filter after GET_MAC.' "$RACFG" >/dev/null
grep -F '!ether_addr_equal(eh->h_dest, inic->mii_mac)' "$RACFG" >/dev/null
grep -F '!ether_addr_equal(eh->h_source, inic->radio[ridx].mac)' "$RACFG" >/dev/null

# Keep primary OEM source evidence in-tree.
grep -F 'RaCfgWaitSyncRsp(pAd, RACFG_CMD_GET_MAC, 0' "$OEM" >/dev/null
grep -F 'else if (len == 35)' "$OEM" >/dev/null
grep -F 'usleep 500000' "$ROOT/package/kernel/rt3662-inic/oem-reference/sphairon/power_wlan_ralink.sh" >/dev/null

echo 'OK: RT3662/RT5392 v8 OEM 2.7.0.5 RACFG bootstrap, passive GET_MAC, 11-TX retry semantics and GPIO recovery'
