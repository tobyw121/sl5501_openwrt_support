#!/usr/bin/env python3
"""Create the SL5501 256 KiB uboot_cfg partition image.

The active U-Boot environment is 8 KiB at the start of the 256 KiB erase
sector.  Lantiq MIPS U-Boot 2013.10 stores the CRC32 in big-endian order.
The remainder of the erase sector stays 0xff so `saveenv` can erase/rewrite
it normally.
"""

import argparse
import binascii
import struct
from pathlib import Path

ENV_SIZE = 0x2000
PART_SIZE = 0x40000
DEFAULT_ETHADDR = "1C:74:0D:BA:28:10"

DEFAULT_VARS = [
    ("bootcmd", "run boot_openwrt"),
    ("bootdelay", "2"),
    ("baudrate", "115200"),
    ("ethaddr", DEFAULT_ETHADDR),
    ("ipaddr", "192.168.1.1"),
    ("serverip", "192.168.1.2"),
    ("netdev", "eth0"),
    ("openwrt_addr", "0x82000000"),
    ("boot_openwrt", "setenv bootargs console=ttyLTQ0,115200 ethaddr=${ethaddr}; sf probe 0:4 25000000 0; sf read ${openwrt_addr} 0x000c0000 0x00f80000; bootm ${openwrt_addr}"),
    ("mtdids", "nor0=spi0.4"),
    ("mtdparts", "mtdparts=spi0.4:512k(uboot_fix),256k(uboot_cfg),15872k(firmware),-(data)"),
    ("boot_try", "0"),
]


def valid_mac(value: str) -> str:
    parts = value.split(":")
    if len(parts) != 6 or any(len(x) != 2 for x in parts):
        raise argparse.ArgumentTypeError("MAC must be XX:XX:XX:XX:XX:XX")
    try:
        octets = bytes(int(x, 16) for x in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("MAC contains non-hex digits") from exc
    if octets[0] & 1:
        raise argparse.ArgumentTypeError("multicast MAC is not valid for ethaddr")
    return ":".join(f"{x:02X}" for x in octets)


def build_partition(ethaddr: str) -> tuple[bytes, int]:
    variables = [(k, ethaddr if k == "ethaddr" else v) for k, v in DEFAULT_VARS]
    body = b"".join(f"{k}={v}".encode("ascii") + b"\0" for k, v in variables) + b"\0"
    if len(body) > ENV_SIZE - 4:
        raise SystemExit(f"SL5501 U-Boot environment too large: {len(body)} bytes")
    data = body + b"\0" * (ENV_SIZE - 4 - len(body))
    crc = binascii.crc32(data) & 0xFFFFFFFF
    env = struct.pack(">I", crc) + data
    return env + b"\xff" * (PART_SIZE - ENV_SIZE), crc


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--ethaddr", type=valid_mac, default=DEFAULT_ETHADDR)
    args = parser.parse_args()

    partition, crc = build_partition(args.ethaddr)
    Path(args.output).write_bytes(partition)
    print(f"SL5501 uboot_cfg: {len(partition)} bytes, ethaddr={args.ethaddr}, env CRC={crc:08x}")


if __name__ == "__main__":
    main()
