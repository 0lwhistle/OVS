#!/usr/bin/env python3
"""
pack_dtb.py - 设备树 JSON → A/B 槽格式容器 (build/dtb.bin)

槽格式（dtb_ab.c 解析，两端必须一致）:
  偏移 0:  magic "DTBI"  u32 LE
  偏移 4:  fmt_version   u8   (当前 1)
  偏移 5:  保留          3B
  偏移 8:  json_len      u32 LE
  偏移 12: sha256(json)  32B
  偏移 44: JSON 原文

用法: pack_dtb.py <ovs.dtb.json> <输出 dtb.bin>
"""
import hashlib
import struct
import sys

MAGIC = b"DTBI"
FMT_VERSION = 1


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]

    with open(src, "rb") as f:
        js = f.read()
    if not js:
        print(f"error: {src} is empty")
        return 1

    digest = hashlib.sha256(js).digest()
    header = struct.pack("<IB3xI32s", int.from_bytes(MAGIC, "little"),
                         FMT_VERSION, len(js), digest)
    with open(dst, "wb") as f:
        f.write(header + js)

    print(f"dtb.bin: {len(js)} bytes json -> {44 + len(js)} bytes container "
          f"(sha256 {digest[:8].hex()}...)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
