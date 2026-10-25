#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PKG="$ROOT/package/kernel/rt3662-inic/Makefile"
MAIN="$ROOT/package/kernel/rt3662-inic/src/rt3662_main.c"
RACFG="$ROOT/package/kernel/rt3662-inic/src/rt3662_racfg.c"
HDR="$ROOT/package/kernel/rt3662-inic/src/rt3662_inic.h"
FWDIR="$ROOT/package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662"

[ "$(wc -c < "$FWDIR/iNIC_ap.bin")" -eq 576932 ]
[ "$(sha256sum "$FWDIR/iNIC_ap.bin" | awk '{print $1}')" = f927ebf5a4ac0eebef037da11490736a6a6f1adc8e900cf42781d76d1ed010cc ]
[ "$(wc -c < "$FWDIR/iNIC_e2p.bin")" -eq 1024 ]
[ "$(sha256sum "$FWDIR/iNIC_e2p.bin" | awk '{print $1}')" = 2d90cbda03ff2430c4789b753aaa3007d5d12252c28dfa3878f8c8d6fd44d66d ]
[ "$(wc -c < "$FWDIR/iNIC_e2p1.bin")" -eq 512 ]
[ "$(sha256sum "$FWDIR/iNIC_e2p1.bin" | awk '{print $1}')" = 049b717a8bc348f60182d030113a1d3099ce5431275f04dbaa74e2467a249fd5 ]

grep -F 'PKG_RELEASE:=23' "$PKG" >/dev/null
grep -F 'RT3662_CRC_HEADER_LEN 40' "$HDR" >/dev/null
grep -F 'RT3662_MAX_FW_UPLOADS 5' "$HDR" >/dev/null
grep -F 'RT3662_GET_MAC_TIMEOUT_MS 40000' "$HDR" >/dev/null
grep -F 'msleep(500);' "$MAIN" >/dev/null
grep -F 'rt3662_enable_master(inic);' "$MAIN" >/dev/null
grep -F 'rt3662_release_reset(inic);' "$MAIN" >/dev/null
grep -F 'dev_set_mac_address(inic->master, &sa, NULL);' "$MAIN" >/dev/null
! grep -F 'RT3662_CMD_BOOT_RESET, 0, 0, 0, 0, NULL, 0' "$MAIN" >/dev/null
grep -F 'module_param(bridge, bool, 0644)' "$MAIN" >/dev/null
grep -F 'module_param(syncmiimac, bool, 0644)' "$MAIN" >/dev/null
grep -F 'module_param(csumoff, bool, 0644)' "$MAIN" >/dev/null
grep -F 'module_param(max_fw_upload, uint, 0644)' "$MAIN" >/dev/null
grep -F 'profile requests external EEPROM' "$RACFG" >/dev/null
grep -F '__SkipIfNotINIC__EXT_EEPROM_SIZE=%zu' "$RACFG" >/dev/null
grep -F 'declared != payload_len' "$RACFG" >/dev/null
grep -F 'payload_crc = crc32(~0U, fw->data, payload_len) ^ ~0U;' "$RACFG" >/dev/null
grep -F 'OEM _upload_firmware() emits BOOT_STARTUP' "$RACFG" >/dev/null
grep -F 'RT3662_GET_MAC_TIMEOUT_MS' "$RACFG" >/dev/null
grep -F 'static bool tx_delay_sweep;' "$RACFG" >/dev/null
grep -F 'tx_delay_sweep && command == RT3662_CMD_BOOT_INITCFG' "$RACFG" >/dev/null

grep -F 'RT_DRV_VERSION="2.7.0.5"' "$FWDIR/RT3662-version" >/dev/null
grep -F 'ExtEEPROM=0' "$FWDIR/iNIC_ap.dat" >/dev/null
grep -F 'ExtEEPROM=0' "$FWDIR/iNIC_ap1.dat" >/dev/null
[ -f "$ROOT/package/kernel/rt3662-inic/oem-reference/ralink-2.4.0.9.10/comm/raconfig.c" ]
[ -f "$ROOT/package/kernel/rt3662-inic/oem-reference/sphairon/rtXXXX_iNIC-2.7.0.5.symbols.txt" ]

python3 - "$FWDIR/iNIC_ap.bin" <<'PY'
import sys, zlib, struct
b=open(sys.argv[1],'rb').read()
t=b[-40:]
assert t[:4] == bytes.fromhex('01081418')
stored=struct.unpack('<I',t[4:8])[0]
declared=struct.unpack('<I',t[-4:])[0]
assert declared == len(b)-40 == 576892
assert zlib.crc32(b[:-40]) & 0xffffffff == stored == 0x7aa4ecdc
PY

echo 'OK: RT3662 v6 OEM 2.7.0.5 source-aligned bootstrap, power sequence, CRC firmware and E2P support'
