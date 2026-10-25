#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT = Path(__file__).resolve().parents[1]
p0 = (ROOT / 'package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap.dat').read_bytes()
p1 = (ROOT / 'package/kernel/rt3662-inic/files/lib/firmware/ralink/rt3662/iNIC_ap1.dat').read_bytes()
assert len(p0) == 2311 and hashlib.sha256(p0).hexdigest() == '86811a1f65e3ef3ba76d9c48a8d95b47ce163f8311175b187ed25b1654b799b1'
assert len(p1) == 2213 and hashlib.sha256(p1).hexdigest() == '872b9d43ab5c6a94033f2d3910a1a1c9e16e9672643bf95f055770b4750428f0'
base = '1C:74:0D:BA:28:10'
r0 = '1C:74:0D:BA:28:1C'
r1 = '1C:74:0D:BA:28:1E'
extra0 = (f'\n__SkipIfNotINIC__MAC={r0}\n'
          f'MiiMasterMac={base}\n'
          '__SkipIfNotINIC__BUILT_IN_BRIDGE=1\n'
          '__SkipIfNotINIC__CONCURRENT_ENABLE=1\n'
          '__SkipIfNotINIC__BIG_ENDIAN=1\n').encode()
extra1 = (f'\n__SkipIfNotINIC__MAC={r1}\n').encode()
stream = p0 + extra0 + b'\n' + p1 + extra1 + b'\0'
chunks = [len(stream[i:i+1024]) for i in range(0, len(stream), 1024)]
assert len(extra0) == 173
assert len(extra1) + 1 == 41
assert len(stream) == 4739
assert chunks == [1024, 1024, 1024, 1024, 643]
print('OK: v22 byte-exact-v20/OEM/UCI-synchronized OEM-geometry profiles -> 4739-byte concurrent INITCFG stream, chunks 1024/1024/1024/1024/643')
