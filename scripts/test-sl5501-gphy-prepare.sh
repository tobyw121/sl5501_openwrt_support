#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
MK="$ROOT/package/boot/uboot-lantiq/Makefile"
FW="$ROOT/target/linux/lantiq/files/firmware/lantiq"

for call in \
  '$(call CompressVR9Firmware,11g,1,4)' \
  '$(call CompressVR9Firmware,11g,2,2)' \
  '$(call CompressVR9Firmware,22f,1,4)' \
  '$(call CompressVR9Firmware,22f,2,2)'; do
  grep -F "$call" "$MK" >/dev/null || { echo "missing: $call" >&2; exit 1; }
done

for f in \
  xrx200_phy11g_a14.bin xrx200_phy11g_a22.bin \
  xrx200_phy22f_a14.bin xrx200_phy22f_a22.bin; do
  [ -s "$FW/$f" ] || { echo "missing firmware: $FW/$f" >&2; exit 1; }
done

echo "OK: all four VR9 GPHY inputs are available and scheduled for U-Boot blob generation"
