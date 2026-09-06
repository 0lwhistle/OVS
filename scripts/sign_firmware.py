#!/usr/bin/env python3
"""
固件签名工具：在固件末尾附加魔术数 + SHA256 哈希

用法：
    签名:   python scripts/sign_firmware.py build/ovs.bin
    验证:   python scripts/sign_firmware.py --verify build/ovs_signed.bin

输出：
    build/ovs_signed.bin  （签名后的固件，用于 OTA 升级）
"""

import sys
import os
import hashlib
import struct

OTA_MAGIC = 0x4F56414F  # "OVAO"

def sign_firmware(input_path, output_path=None):
    """在固件末尾附加校验信息"""
    
    if not os.path.exists(input_path):
        print(f"Error: Firmware not found: {input_path}")
        sys.exit(1)
    
    if output_path is None:
        base, ext = os.path.splitext(input_path)
        output_path = f"{base}_signed{ext}"
    
    # 读取固件
    with open(input_path, 'rb') as f:
        firmware = f.read()
    
    fw_size = len(firmware)
    print(f"Firmware: {input_path}")
    print(f"  Size:   {fw_size} bytes ({fw_size/1024:.1f} KB)")
    
    # 计算 SHA256
    sha256_hash = hashlib.sha256(firmware).digest()
    print(f"  SHA256: {sha256_hash.hex()}")
    
    # 构造校验尾
    # struct: magic(4B) + sha256(32B) + firmware_size(4B) = 40 bytes
    footer = struct.pack('<I', OTA_MAGIC) + sha256_hash + struct.pack('<I', fw_size)
    
    # 写入签名后的固件
    with open(output_path, 'wb') as f:
        f.write(firmware)
        f.write(footer)
    
    signed_size = fw_size + len(footer)
    print(f"Signed:  {output_path}")
    print(f"  Size:   {signed_size} bytes ({signed_size/1024:.1f} KB)")
    print(f"  Footer: {len(footer)} bytes (magic + SHA256 + size)")
    print()
    print(f"OTA command:")
    print(f"  ./scripts/ota_update.sh <esp32-ip> {output_path}")
    
    return output_path

def verify_firmware(path):
    """验证已签名的固件"""
    
    if not os.path.exists(path):
        print(f"Error: File not found: {path}")
        return False
    
    with open(path, 'rb') as f:
        data = f.read()
    
    if len(data) < 44:  # 最小大小: 4B magic + 32B sha256 + 4B size + 至少 4B 固件
        print("ERROR: File too small to contain valid signature!")
        return False
    
    # 从末尾查找魔术数
    footer_offset = None
    for i in range(len(data) - 4, max(len(data) - 1024, 0), -1):
        magic = struct.unpack('<I', data[i:i+4])[0]
        if magic == OTA_MAGIC:
            footer_offset = i
            break
    
    if footer_offset is None:
        print("ERROR: No valid signature found!")
        return False
    
    # 解析校验尾
    footer = data[footer_offset:]
    magic, sha256_expected, fw_size = struct.unpack('<I32sI', footer)
    
    if fw_size != footer_offset:
        print(f"ERROR: Firmware size mismatch! (header says {fw_size}, actual offset is {footer_offset})")
        return False
    
    firmware = data[:fw_size]
    sha256_actual = hashlib.sha256(firmware).digest()
    
    print(f"Verifying: {path}")
    print(f"  Firmware size: {fw_size} bytes ({fw_size/1024:.1f} KB)")
    print(f"  Expected SHA256: {sha256_expected.hex()}")
    print(f"  Actual SHA256:   {sha256_actual.hex()}")
    
    if sha256_expected == sha256_actual:
        print("  Result: VERIFIED OK!")
        return True
    else:
        print("  Result: SHA256 MISMATCH!")
        return False

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage:")
        print("  Sign:   python scripts/sign_firmware.py <firmware.bin>")
        print("  Verify: python scripts/sign_firmware.py --verify <signed.bin>")
        sys.exit(1)
    
    if sys.argv[1] == '--verify':
        if len(sys.argv) < 3:
            print("Usage: python scripts/sign_firmware.py --verify <signed.bin>")
            sys.exit(1)
        ok = verify_firmware(sys.argv[2])
        sys.exit(0 if ok else 1)
    else:
        sign_firmware(sys.argv[1])
