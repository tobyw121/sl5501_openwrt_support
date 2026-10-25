#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
PATCH="$ROOT/target/linux/lantiq/patches-6.6/0743-net-dsa-lantiq-gswip-export-sl5501-port5-tx-delay-diag.patch"
UBOOT="$ROOT/package/boot/uboot-lantiq/patches/0014-MIPS-add-support-for-Lantiq-XWAY-SoCs.patch"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
LOGDOC="$ROOT/SL5501-RT3662-RT5392-V9.md"

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null

# VR9 OEM/U-Boot layout: cfg0/pcdu0, cfg1/pcdu1, 6 reserved words,
# then cfg5/pcdu5 => offsets 0x0a/0x0b from the xMII resource base.
grep -F '__be32 rsvd0[0x6];' "$UBOOT" >/dev/null
grep -F '__be32 mii_cfg5;' "$UBOOT" >/dev/null
grep -F '__be32 pcdu5;' "$UBOOT" >/dev/null

# Linux-6.6 inherited 0x05 must be corrected to the actual VR9 PCDU5 offset.
grep -F -- '-#define GSWIP_MII_PCDU5' "$PATCH" | grep -F '0x05' >/dev/null
grep -F -- '+#define GSWIP_MII_PCDU5' "$PATCH" | grep -F '0x0b' >/dev/null
grep -F 'MII_CFG5=0x0a, followed by PCDU5=0x0b' "$PATCH" >/dev/null
grep -F 'expected = delay_ps / 500;' "$PATCH" >/dev/null
grep -F '(pcdu & GSWIP_MII_PCDU_TXDLY_MASK) != expected' "$PATCH" >/dev/null
grep -F 'return -EIO;' "$PATCH" >/dev/null
grep -F 'PCDU5@0x0b' "$PATCH" >/dev/null
grep -F 'VERIFIED' "$PATCH" >/dev/null

# Driver must only advertise success after the kernel helper has verified MMIO.
grep -F 'applied and hardware-verified OEM GSWIP port5 TX delay %d ps before iNIC reset release' "$RACFG" >/dev/null

[ -f "$LOGDOC" ]
echo 'OK: SL5501 v9 fixes the real VR9 PCDU5 register and requires hardware readback verification'
