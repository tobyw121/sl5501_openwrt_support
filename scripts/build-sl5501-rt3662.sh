#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

case "$ROOT" in
  *'#'*)
    echo "ERROR: OpenWrt build path contains '#': $ROOT" >&2
    exit 2
    ;;
esac

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
V="${V:-s}"

# OpenWrt's package-only kernel-module target assumes that the target kernel
# has already been configured and built.  After target/linux/clean, prepare
# and compile it first so .config, Module.symvers and modules.builtin exist.
cp configs/sl5501_defconfig .config
make defconfig
make target/linux/prepare -j1 V="$V"
make target/linux/compile -j"$JOBS" V="$V"
make package/kernel/rt3662-inic/compile -j1 V="$V"

echo "SL5501 RT3662/RT5392 kernel module build completed."
