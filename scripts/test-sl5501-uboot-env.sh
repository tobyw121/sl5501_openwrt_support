#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/sl5501-uboot-cfg-test.$$"
trap 'rm -f "$TMP"' EXIT HUP INT TERM
python3 "$ROOT/scripts/mk-sl5501-uboot-env.py" --output "$TMP" >/dev/null
python3 - "$TMP" <<'PY'
import binascii, struct, sys
from pathlib import Path
p=Path(sys.argv[1]).read_bytes()
assert len(p)==0x40000, len(p)
env=p[:0x2000]
stored=struct.unpack('>I',env[:4])[0]
calc=binascii.crc32(env[4:]) & 0xffffffff
assert stored==calc, (hex(stored),hex(calc))
vars={}
for raw in env[4:].split(b'\0'):
    if not raw:
        break
    k,v=raw.split(b'=',1)
    vars[k.decode()]=v.decode()
assert vars['ethaddr']=='1C:74:0D:BA:28:10'
assert vars['bootcmd']=='run boot_openwrt'
assert vars['mtdparts']=='mtdparts=spi0.4:512k(uboot_fix),256k(uboot_cfg),15872k(firmware),-(data)'
assert p[0x2000:]==b'\xff'*(0x40000-0x2000)
print(f"OK env CRC={stored:08x} ethaddr={vars['ethaddr']}")
PY
