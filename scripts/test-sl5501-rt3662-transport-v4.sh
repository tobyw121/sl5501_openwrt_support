#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null
grep -F 'memcpy(eth, rt3662_boot_ethhdr, ETH_HLEN);' "$RACFG" >/dev/null
grep -F 'skb_reserve(skb, NET_SKB_PAD + 2);' "$RACFG" >/dev/null
! grep -F 'boot_unicast_source' "$RACFG" >/dev/null
! grep -F 'switching reply path to unicast' "$RACFG" >/dev/null

echo 'OK: historical v4 transport regression superseded; v8 keeps exact OEM BootCmdHdr without unicast fallback'
