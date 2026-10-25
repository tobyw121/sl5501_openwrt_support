#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
PATCH="$ROOT/target/linux/lantiq/patches-6.6/0743-net-dsa-lantiq-gswip-export-sl5501-port5-tx-delay-diag.patch"
BUILD="$ROOT/scripts/build-sl5501-rt3662.sh"

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null
grep -F -- '+#define GSWIP_MII_PCDU5' "$PATCH" | grep -F '0x0b' >/dev/null
grep -F '(unsigned int)((pcdu & GSWIP_MII_PCDU_TXDLY_MASK) * 500)' "$PATCH" >/dev/null
grep -F '(unsigned int)(((pcdu & GSWIP_MII_PCDU_RXDLY_MASK) >> 7) * 500)' "$PATCH" >/dev/null
grep -F 'make target/linux/prepare -j1' "$BUILD" >/dev/null
grep -F 'make target/linux/compile -j"$JOBS"' "$BUILD" >/dev/null
grep -F 'make package/kernel/rt3662-inic/compile -j1' "$BUILD" >/dev/null

echo 'OK: SL5501 v10 fixes PCDU5 format-Werror and safe isolated RT3662 build ordering'
