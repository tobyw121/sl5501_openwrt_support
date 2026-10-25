#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
KPATCH="$ROOT/target/linux/lantiq/patches-6.6/0743-net-dsa-lantiq-gswip-export-sl5501-port5-tx-delay-diag.patch"

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null
grep -F 'lantiq_gswip_sl5501_set_port5_tx_delay' "$KPATCH" >/dev/null
grep -F 'EXPORT_SYMBOL_GPL(lantiq_gswip_sl5501_set_port5_tx_delay)' "$KPATCH" >/dev/null
grep -F 'EXPORT_SYMBOL_GPL(lantiq_gswip_sl5501_get_port5_tx_stats)' "$KPATCH" >/dev/null
grep -F '2000, 1500, 2500, 1000, 3000, 500, 3500, 0' "$RACFG" >/dev/null
grep -F 'static bool tx_delay_sweep;' "$RACFG" >/dev/null
grep -F 'module_param(tx_delay_sweep, bool, 0644);' "$RACFG" >/dev/null
grep -F 'diagnostic GSWIP port5 TX RMON:' "$RACFG" >/dev/null
grep -F 'tx_delay_sweep && command == RT3662_CMD_BOOT_INITCFG' "$RACFG" >/dev/null
! grep -F 'switching reply path to unicast' "$RACFG" >/dev/null

echo 'OK: RT3662 v5 GSWIP port5 TX-delay/RMON diagnostics retained as optional diagnostic in current v9'
