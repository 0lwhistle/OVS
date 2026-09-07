#!/usr/bin/env bash
#
# 构建脚本：编译 Vue 前端 + 打包 Web 资源 + 编译固件 + 签名 + 烧录
#
# 用法: bash ./scripts/mybuild.sh [--clean] [串口]
#   --clean    清理 build 目录（默认开启）
#   串口       烧录串口，默认 /dev/ttyACM0
#
# 示例:
#   bash ./scripts/mybuild.sh
#   bash ./scripts/mybuild.sh /dev/ttyUSB0
#   bash ./scripts/mybuild.sh --clean /dev/ttyUSB0
#

# 确保使用 bash 执行
if [ -z "$BASH_VERSION" ]; then
    echo "Error: This script must be run with bash, not sh"
    echo "Usage: bash ./scripts/mybuild.sh"
    exit 1
fi

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# 解析参数
CLEAN_BUILD=true
SERIAL_PORT=""
for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN_BUILD=true
            ;;
        /dev/*)
            SERIAL_PORT="$arg"
            ;;
    esac
done

# ESP-IDF 环境
echo -e "${YELLOW}Activating ESP-IDF...${NC}"
ESP_IDF_EXPORT="/home/olwhistle/dockerNow/esp32/ESP-IDF/esp-idf-v6.0.1/export.sh"

if [ -f "$ESP_IDF_EXPORT" ]; then
    # 清理 PATH：保留 Linux 基本路径，NVM 放最前面确保新版 Node.js
    CLEAN_PATH="/root/.nvm/versions/node/v24.16.0/bin:/root/.local/bin:/root/.espressif/python_env/idf6.0_py3.10_env/bin:/root/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:/root/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    export PATH="$CLEAN_PATH"
    
    source "$ESP_IDF_EXPORT" 2>/dev/null || true
else
    echo -e "${YELLOW}Warning: ESP-IDF export.sh not found at $ESP_IDF_EXPORT${NC}"
    echo "Please check your ESP-IDF installation path"
    exit 1
fi

# 清理 build 目录（如果需要）
if [ "$CLEAN_BUILD" = true ] && [ -d "build" ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf build
    echo -e "${GREEN}  Done!${NC}"
    echo ""
fi

# 检查 build 目录配置是否匹配
if [ -d "build" ] && [ -f "build/project_description.json" ]; then
    BUILD_PROJECT_DIR=$(python3 -c "import json; print(json.load(open('build/project_description.json'))['project_dir'])" 2>/dev/null || echo "")
    CURRENT_DIR=$(pwd)
    if [ "$BUILD_PROJECT_DIR" != "$CURRENT_DIR" ]; then
        echo -e "${YELLOW}Build directory configured for different path, cleaning...${NC}"
        rm -rf build
        echo -e "${GREEN}  Done!${NC}"
        echo ""
    fi
fi

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  OVS Build Script${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# 1. 构建 Vue 前端
echo -e "${YELLOW}[1/4] Building Vue frontend...${NC}"
cd "$PROJECT_ROOT/web/vue-ui"
npm install --silent
npm run build
cd "$PROJECT_ROOT"
echo -e "${GREEN}  Done!${NC}"
echo ""

# 2. 将 Vue 构建产物打包为 C 代码
echo -e "${YELLOW}[2/4] Packing web resources...${NC}"
python3 "$PROJECT_ROOT/scripts/fs_to_c.py" \
    "$PROJECT_ROOT/web/vue-ui/dist" \
    "$PROJECT_ROOT/components/modules/web/web_data"
echo -e "${GREEN}  Done!${NC}"
echo ""

# 3. 编译固件
echo -e "${YELLOW}[3/4] Building firmware...${NC}"
idf.py build
echo -e "${GREEN}  Done!${NC}"
echo ""

# 4. 签名固件（用于 OTA 升级）
echo -e "${YELLOW}[4/4] Signing firmware for OTA...${NC}"
python3 "$PROJECT_ROOT/scripts/sign_firmware.py" "$PROJECT_ROOT/build/ovs.bin"
echo ""

# 5. 自动烧录
echo -e "${YELLOW}[5/5] Flashing firmware...${NC}"
FLASH_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
echo "  Port: $FLASH_PORT"
echo ""

# 烧录固件（包括 SPIFFS）
idf.py -p "$FLASH_PORT" flash

echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "${GREEN}  Build & Flash complete!${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo "Firmware: build/ovs.bin"
echo "Signed:   build/ovs_signed.bin"
echo ""
echo "Monitor:  idf.py -p $FLASH_PORT monitor"
echo "OTA:      ./scripts/ota_update.sh <esp32-ip>"
