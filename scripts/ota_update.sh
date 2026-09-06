#!/usr/bin/env bash
#
# OTA 固件升级脚本（支持断点续传）
#
# 用法: bash ./scripts/ota_update.sh <esp32-ip> [firmware.bin]
#
# 示例:
#   bash ./scripts/ota_update.sh 192.168.2.154
#   bash ./scripts/ota_update.sh 192.168.2.154 build/ovs_signed.bin
#

# 确保使用 bash 执行
if [ -z "$BASH_VERSION" ]; then
    echo "Error: This script must be run with bash, not sh"
    echo "Usage: bash ./scripts/ota_update.sh <esp32-ip> [firmware.bin]"
    exit 1
fi

set -e

# ---- 颜色定义 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ---- 项目根目录 ----
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# ---- 参数解析 ----
if [ $# -lt 1 ]; then
    echo -e "${RED}Usage: $0 <esp32-ip> [firmware.bin]${NC}"
    echo ""
    echo "Examples:"
    echo "  $0 192.168.2.154"
    echo "  $0 192.168.2.154 build/ovs_signed.bin"
    exit 1
fi

ESP_IP="$1"
FIRMWARE="${2:-build/ovs_signed.bin}"

# ---- 检查固件是否存在，不存在则签名 ----
if [ ! -f "$FIRMWARE" ]; then
    # 尝试从 build/ovs.bin 签名
    if [ -f "build/ovs.bin" ]; then
        echo -e "${YELLOW}Signed firmware not found, signing build/ovs.bin...${NC}"
        python3 "$PROJECT_ROOT/scripts/sign_firmware.py" build/ovs.bin
        FIRMWARE="build/ovs_signed.bin"
    else
        echo -e "${RED}Error: Firmware not found: $FIRMWARE${NC}"
        echo "Build the firmware first: bash ./scripts/mybuild.sh"
        exit 1
    fi
fi

# ---- 获取固件信息 ----
FW_SIZE=$(stat -c%s "$FIRMWARE")
FW_SIZE_KB=$(echo "scale=1; $FW_SIZE / 1024" | bc)

echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  OTA Firmware Update${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "  Target:   ${GREEN}$ESP_IP:80${NC}"
echo -e "  Firmware: ${YELLOW}$FIRMWARE${NC}"
echo -e "  Size:     ${YELLOW}$FW_SIZE bytes ($FW_SIZE_KB KB)${NC}"
echo ""

# ---- 本地验证固件签名 ----
echo -e "${CYAN}[1/4] Verifying firmware signature locally...${NC}"
python3 "$PROJECT_ROOT/scripts/sign_firmware.py" --verify "$FIRMWARE"
echo ""

# ---- 查询 ESP32 上的 OTA 进度（断点续传） ----
echo -e "${CYAN}[2/4] Checking OTA progress on ESP32...${NC}"
PROGRESS_RESP=$(curl -s "http://$ESP_IP/ota/progress" 2>&1 || echo "")
WRITTEN=0
TOTAL=0

if [ -n "$PROGRESS_RESP" ]; then
    # 尝试解析 JSON 响应
    WRITTEN=$(echo "$PROGRESS_RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('written',0))" 2>/dev/null || echo "0")
    TOTAL=$(echo "$PROGRESS_RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('total',0))" 2>/dev/null || echo "0")
fi

echo -e "  Written: ${YELLOW}$WRITTEN${NC} / Total: ${YELLOW}$TOTAL${NC} bytes"

# 判断是否需要续传
RESUME_OFFSET=0
if [ "$WRITTEN" -gt 0 ] && [ "$WRITTEN" -lt "$FW_SIZE" ]; then
    echo -e "  ${GREEN}Resuming from offset $WRITTEN (previous upload was interrupted)${NC}"
    RESUME_OFFSET=$WRITTEN
elif [ "$WRITTEN" -ge "$FW_SIZE" ]; then
    echo -e "  ${YELLOW}Firmware already fully uploaded! Rebooting...${NC}"
    # 触发完成（如果设备还没重启）
    curl -s -X POST --data-binary @/dev/null \
         -H "X-Offset: $WRITTEN" \
         "http://$ESP_IP/ota/update" > /dev/null 2>&1 || true
    exit 0
else
    echo -e "  ${GREEN}Starting fresh upload${NC}"
fi
echo ""

# ---- 上传固件（支持断点续传） ----
echo -e "${CYAN}[3/4] Uploading firmware to ESP32...${NC}"

if [ "$RESUME_OFFSET" -gt 0 ]; then
    echo -e "  ${YELLOW}Resume mode: skipping first $RESUME_OFFSET bytes${NC}"
else
    echo -e "  ${YELLOW}Full upload mode${NC}"
fi
echo ""

# 使用 dd 跳过已上传的部分，用 curl 上传剩余部分
if [ "$RESUME_OFFSET" -gt 0 ]; then
    # 续传模式：只上传剩余部分
    HTTP_CODE=$(dd if="$FIRMWARE" bs=1024 skip=$((RESUME_OFFSET / 1024)) 2>/dev/null | \
        curl -s -o /tmp/ota_response_$$.json -w "%{http_code}" \
            -X POST \
            --data-binary @- \
            -H "X-Offset: $RESUME_OFFSET" \
            --progress-bar \
            "http://$ESP_IP/ota/update")
else
    # 全量上传
    HTTP_CODE=$(curl -s -o /tmp/ota_response_$$.json -w "%{http_code}" \
        -X POST \
        --data-binary @"$FIRMWARE" \
        --progress-bar \
        "http://$ESP_IP/ota/update")
fi

# ---- 检查上传结果 ----
echo -e "${CYAN}[4/4] Checking upload result...${NC}"

if [ "$HTTP_CODE" -eq 200 ]; then
    echo -e "${GREEN}  Upload successful!${NC}"
    
    # 读取响应
    if [ -f /tmp/ota_response_$$.json ]; then
        echo "  Response:"
        cat /tmp/ota_response_$$.json | python3 -m json.tool 2>/dev/null || cat /tmp/ota_response_$$.json
        rm -f /tmp/ota_response_$$.json
    fi
    
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  OTA Update Complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "ESP32 will reboot automatically..."
else
    echo -e "${RED}  Upload failed! (HTTP $HTTP_CODE)${NC}"
    
    # 读取错误响应
    if [ -f /tmp/ota_response_$$.json ]; then
        echo "  Error response:"
        cat /tmp/ota_response_$$.json | python3 -m json.tool 2>/dev/null || cat /tmp/ota_response_$$.json
        rm -f /tmp/ota_response_$$.json
    fi
    
    exit 1
fi
