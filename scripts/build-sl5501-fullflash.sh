#!/bin/sh
set -eu

TOPDIR="${1:-.}"
BACKUP="${SL5501_SPI_BACKUP:-${2:-$TOPDIR/backup_SPI_008.bin}}"
OUT="${SL5501_SPI_OUTPUT:-$TOPDIR/bin/targets/lantiq/xrx200/openwrt-lantiq-xrx200-sphairon_sl5501-squashfs-fullflash-64m.bin}"
ENVOUT="${SL5501_ENV_OUTPUT:-$TOPDIR/bin/targets/lantiq/xrx200/sl5501-uboot_cfg-openwrt.bin}"

if [ ! -f "$BACKUP" ]; then
    echo "ERROR: 64 MiB SL5501 SPI backup not found: $BACKUP" >&2
    echo "Set SL5501_SPI_BACKUP=/path/to/backup_SPI_008.bin" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"
python3 "$TOPDIR/scripts/build-sl5501-spi-image.py" \
    --tree "$TOPDIR" \
    --backup "$BACKUP" \
    --env-output "$ENVOUT" \
    --output "$OUT"

sha256sum "$OUT" > "$OUT.sha256"
echo "Fullflash ready: $OUT"
cat "$OUT.sha256"
