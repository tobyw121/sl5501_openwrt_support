#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
fail() { echo "FAIL: $*" >&2; exit 1; }
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
BASE="$ROOT/package/base-files/Makefile"
GEN="$ROOT/package/base-files/files/bin/config_generate"
BOARD="$ROOT/target/linux/lantiq/xrx200/base-files/etc/board.d/02_network"
WORKER="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-autostart"
DIAG="$ROOT/package/kernel/rt3662-inic/files/usr/sbin/sl5501-wifi-diag"

for f in "$PKG" "$BASE" "$GEN" "$BOARD" "$WORKER" "$DIAG"; do [ -f "$f" ] || fail "missing $f"; done
[ -x "$GEN" ] || fail 'config_generate is not executable'
grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null || fail 'rt3662 package release != 22'
grep -F 'chmod 0755 $(1)/bin/board_detect $(1)/bin/config_generate' "$BASE" >/dev/null || fail 'base-files does not force config_generate executable mode'
grep -F '[ -s "$CFG" ] || /bin/board_detect || exit 1' "$GEN" >/dev/null || fail 'config_generate does not build board.json'
grep -F 'generate_network lan' "$GEN" >/dev/null || fail 'config_generate does not generate LAN'
grep -F 'generate_network wan' "$GEN" >/dev/null || fail 'config_generate does not generate WAN'
grep -F "device=\"br-\$name\"" "$GEN" >/dev/null || fail 'config_generate does not create LAN bridge device'
grep -F 'ucidef_set_interfaces_lan_wan "lan1 lan2 lan3 lan4 wifi" "wan"' "$BOARD" >/dev/null || fail 'SL5501 board LAN/WAN topology missing wifi dataplane'
grep -F 'network UCI missing after base boot; running late /bin/config_generate fallback' "$WORKER" >/dev/null || fail 'autostart has no serialized late network-UCI recovery'
grep -F 'while [ -e /tmp/.config_pending ]' "$WORKER" >/dev/null || fail 'autostart does not serialize against stock first-boot config generation'
grep -F 'network reload rc=' "$WORKER" >/dev/null || fail 'autostart does not verify netifd network reload'
grep -F 'failed-network-reload' "$WORKER" >/dev/null || fail 'autostart has no deterministic network reload failure state'
grep -F 'section network-uci' "$DIAG" >/dev/null || fail 'diag does not dump network UCI'
grep -F 'section board-json' "$DIAG" >/dev/null || fail 'diag does not dump board.json/config_generate state'
sh -n "$GEN"
sh -n "$WORKER"
sh -n "$DIAG"
echo 'OK: v14 restores first-boot network/system generation before dual-band Wi-Fi bring-up'
