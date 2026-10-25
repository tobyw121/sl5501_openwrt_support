#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

case "$ROOT" in
  *'#'*)
    echo "ERROR: OpenWrt build path contains '#': $ROOT" >&2
    echo "Move the tree to a path without '#'; GNU make treats it as a comment." >&2
    exit 2
    ;;
esac

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
V="${V:-s}"

if [ "${CLEAN:-0}" != 0 ]; then
  rm -rf "staging_dir/target-mips_24kc_musl" staging_dir/packages bin/targets/lantiq tmp
fi

cp configs/sl5501_defconfig .config
make defconfig

# Build the bootloader first so a U-Boot patch error is reported early instead
# of after an otherwise long firmware build.
make package/boot/uboot-lantiq/compile -j"$JOBS" V="$V"

# Then build the complete SL5501 firmware.
make -j"$JOBS" V="$V"

FULLFLASH="$ROOT/bin/targets/lantiq/xrx200/openwrt-lantiq-xrx200-sphairon_sl5501-fullflash-spi.bin"
if [ -f "$FULLFLASH" ]; then
  [ "$(wc -c < "$FULLFLASH" | tr -d ' ')" = 67108864 ] || {
    echo "ERROR: generated SL5501 fullflash is not exactly 64 MiB: $FULLFLASH" >&2
    exit 1
  }
  sha256sum "$FULLFLASH" > "$FULLFLASH.sha256"
  echo "SL5501 fullflash SPI image: $FULLFLASH"
  cat "$FULLFLASH.sha256"
else
  echo "ERROR: normal OpenWrt build did not create the SL5501 fullflash SPI artifact" >&2
  exit 1
fi
