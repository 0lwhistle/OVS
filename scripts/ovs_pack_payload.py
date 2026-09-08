#!/usr/bin/env python3
"""
ovs_pack_payload.py - 打包 OTA 上传容器 (app + 可选设备树)

容器格式（web_ota.c 解析，两端必须一致）:
  偏移 0:  magic "OVSO"   u32 LE
  偏移 4:  fmt_version    u8   (当前 1)
  偏移 5:  flags          u8   (bit0 = 含设备树)
  偏移 6:  保留           2B
  偏移 8:  app_size       u32 LE
  偏移 12: dtb_size       u32 LE
  偏移 16: sha256(app)    32B
  偏移 48: sha256(dtb)    32B（无 dtb 时全 0）
  偏移 80: 保留填充至     96B
  偏移 96: app.bin 原文
  之后:    dtb.bin 原文（如有）

不含 magic 的旧式上传（纯 app 流）依旧被设备端兼容。

用法: ovs_pack_payload.py <app.bin> [dtb.bin] -o <payload.bin>
"""
import argparse
import hashlib
import struct

MAGIC = b"OVSO"
FMT_VERSION = 1
HEADER_SIZE = 96


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("app")
    ap.add_argument("dtb", nargs="?")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    with open(args.app, "rb") as f:
        app = f.read()
    dtb = b""
    if args.dtb:
        with open(args.dtb, "rb") as f:
            dtb = f.read()

    flags = 1 if dtb else 0
    header = struct.pack("<IBB2xII32s32s",
                         int.from_bytes(MAGIC, "little"),
                         FMT_VERSION, flags,
                         len(app), len(dtb),
                         hashlib.sha256(app).digest(),
                         hashlib.sha256(dtb).digest())
    header += b"\x00" * (HEADER_SIZE - len(header))

    with open(args.out, "wb") as f:
        f.write(header + app + dtb)

    total = HEADER_SIZE + len(app) + len(dtb)
    print(f"payload: app {len(app)}B + dtb {len(dtb)}B = {total}B "
          f"(flags=0x{flags:02x}) -> {args.out}")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
