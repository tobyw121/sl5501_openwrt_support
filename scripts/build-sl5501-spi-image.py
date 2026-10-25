#!/usr/bin/env python3
import argparse, binascii, hashlib, os, struct, sys
from pathlib import Path

FLASH_SIZE = 0x04000000
UBOOT_OFF, UBOOT_SIZE = 0x00000000, 0x00080000
ENV_OFF, ENV_PART_SIZE, ENV_SIZE = 0x00080000, 0x00040000, 0x00002000
FW_OFF, FW_SIZE = 0x000C0000, 0x00F80000
DATA_OFF, DATA_SIZE = 0x01040000, 0x02FC0000
UIMAGE_MAGIC = 0x27051956
DEFAULT_ETHADDR = '1C:74:0D:BA:28:10'

NEW_VARS = {
    'bootcmd': 'run boot_openwrt',
    'openwrt_addr': '0x82000000',
    'boot_openwrt': 'sf probe 0:4 25000000 0; sf read ${openwrt_addr} 0x000c0000 0x00f80000; bootm ${openwrt_addr}',
    'mtdids': 'nor0=spi0.4',
    'mtdparts': 'mtdparts=spi0.4:512k(uboot_fix),256k(uboot_cfg),15872k(firmware),-(data)',
}

def sha256(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda:f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()

def parse_env(block):
    if len(block) != ENV_SIZE:
        raise ValueError('environment block must be 0x2000 bytes')
    stored = struct.unpack('>I', block[:4])[0]
    calc = binascii.crc32(block[4:]) & 0xffffffff
    if stored != calc:
        raise ValueError(f'U-Boot env CRC mismatch: stored={stored:08x} calc={calc:08x}')
    out=[]
    for raw in block[4:].split(b'\0'):
        if not raw: break
        if b'=' not in raw: continue
        k,v=raw.split(b'=',1)
        out.append((k.decode('ascii'),v.decode('ascii')))
    return out

def build_env(existing):
    order=[]; vals={}
    for k,v in existing:
        if k not in vals: order.append(k)
        vals[k]=v
    for k,v in NEW_VARS.items():
        if k not in vals: order.append(k)
        vals[k]=v
    if not vals.get('ethaddr'):
        if 'ethaddr' not in order:
            order.append('ethaddr')
        vals['ethaddr'] = DEFAULT_ETHADDR
    # Avoid stale vendor auto-boot state overriding direct OpenWrt boot.
    vals['boot_try']='0'
    if 'boot_try' not in order: order.append('boot_try')
    body=b''.join(f'{k}={vals[k]}'.encode('ascii')+b'\0' for k in order)+b'\0'
    if len(body) > ENV_SIZE-4:
        raise ValueError(f'environment too large: {len(body)} > {ENV_SIZE-4}')
    data=body + bytes((ENV_SIZE-4)-len(body))
    crc=binascii.crc32(data) & 0xffffffff
    return struct.pack('>I',crc)+data, vals

def find_one(root, patterns, what):
    hits=[]
    for pat in patterns:
        hits += list(root.glob(pat))
    hits=[p for p in hits if p.is_file()]
    # exact unique by resolved path
    uniq=[]; seen=set()
    for p in hits:
        s=str(p.resolve())
        if s not in seen: uniq.append(p); seen.add(s)
    if not uniq:
        raise FileNotFoundError(f'could not find {what}; pass it explicitly')
    uniq.sort(key=lambda p:p.stat().st_mtime, reverse=True)
    return uniq[0]

def validate_fw(data):
    if len(data) > FW_SIZE:
        raise ValueError(f'firmware too large: {len(data)} > {FW_SIZE}')
    if len(data) < 64:
        raise ValueError('firmware is too small')
    magic=struct.unpack('>I',data[:4])[0]
    if magic != UIMAGE_MAGIC:
        raise ValueError(f'factory image does not start with legacy uImage magic: {magic:08x}')
    ksize=struct.unpack('>I',data[12:16])[0]
    if 64+ksize > len(data):
        raise ValueError(f'uImage kernel size {ksize} exceeds factory image')
    sq=data.find(b'hsqs',64+ksize)
    return ksize, sq

def main():
    ap=argparse.ArgumentParser(description='Build a 64 MiB directly flashable SL5501 SPI image')
    ap.add_argument('--tree', default='.', help='OpenWrt tree used to auto-find build artifacts')
    ap.add_argument('--uboot', help='sl5501_sfspl u-boot.ltq.lzo.sfspl')
    ap.add_argument('--firmware', help='SL5501 squashfs factory.bin')
    ap.add_argument('--backup', help='64 MiB original SPI backup; preserves device U-Boot identity/MAC environment')
    ap.add_argument('-o','--output', default='sl5501-openwrt-fullflash-64m.bin')
    ap.add_argument('--env-output', default='sl5501-uboot_cfg-openwrt.bin')
    args=ap.parse_args()
    root=Path(args.tree)
    uboot=Path(args.uboot) if args.uboot else find_one(root,[
        'bin/targets/lantiq/xrx200/u-boot-sl5501_sfspl/*u-boot.ltq.lzo.sfspl',
        'bin/targets/lantiq/xrx200/**/*sl5501_sfspl*u-boot.ltq.lzo.sfspl',
        'staging_dir/target-*/image/sl5501_sfspl-u-boot.ltq.lzo.sfspl',
    ],'SL5501 SPI-SPL U-Boot')
    firmware=Path(args.firmware) if args.firmware else find_one(root,[
        'bin/targets/lantiq/xrx200/*sphairon_sl5501*squashfs*factory.bin',
        'bin/targets/lantiq/xrx200/*sl5501*factory.bin',
    ],'SL5501 factory.bin')

    ub=uboot.read_bytes(); fw=firmware.read_bytes()
    if len(ub)>UBOOT_SIZE: raise SystemExit(f'U-Boot too large: {len(ub)} > {UBOOT_SIZE}')
    ksize,sq=validate_fw(fw)

    existing=[]
    if args.backup:
        bp=Path(args.backup).read_bytes()
        if len(bp)!=FLASH_SIZE: raise SystemExit(f'backup must be exactly 64 MiB, got {len(bp)} bytes')
        existing=parse_env(bp[ENV_OFF:ENV_OFF+ENV_SIZE])
    env, vals=build_env(existing)
    envpart=env + b'\xff'*(ENV_PART_SIZE-len(env))
    Path(args.env_output).write_bytes(envpart)

    image=bytearray(b'\xff')*FLASH_SIZE
    image[UBOOT_OFF:UBOOT_OFF+len(ub)] = ub
    image[ENV_OFF:ENV_OFF+ENV_PART_SIZE] = envpart
    image[FW_OFF:FW_OFF+len(fw)] = fw
    out=Path(args.output); out.write_bytes(image)

    print('SL5501 64 MiB SPI image built')
    print(f'  U-Boot:   {uboot} ({len(ub)} bytes, sha256 {hashlib.sha256(ub).hexdigest()})')
    print(f'  env:      {args.env_output} ({len(envpart)} bytes, CRC {env[:4].hex()})')
    if vals.get('ethaddr'): print(f'  ethaddr:  {vals["ethaddr"]}')
    print(f'  firmware: {firmware} ({len(fw)} bytes, kernel {ksize} bytes, squashfs @ {hex(sq) if sq>=0 else "not-found"})')
    print(f'  output:   {out} ({out.stat().st_size} bytes)')
    print(f'  sha256:   {sha256(out)}')
    print('  layout:   00000000-0007ffff uboot_fix')
    print('            00080000-000bffff uboot_cfg')
    print('            000c0000-0103ffff firmware')
    print('            01040000-03ffffff data (erased/FF)')

if __name__=='__main__':
    try: main()
    except Exception as e:
        print(f'ERROR: {e}', file=sys.stderr); sys.exit(1)
