#!/usr/bin/env bash
# OVS 开发环境：source 本文件后可直接使用
#   - idf.py / esp-idf 全套工具
#   - scripts/ 下所有脚本（mybuild.sh、ota_push.sh、ovs_release 等，免路径）
# 用法: source scripts/env.sh   （或 . scripts/env.sh）

# 本文件位置 → 项目根目录（不依赖调用时的 cwd）
_OVS_ENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_OVS_ROOT="$(dirname "$_OVS_ENV_DIR")"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

# ---- ESP-IDF 环境 ----
echo -e "${YELLOW}Activating ESP-IDF...${NC}"
ESP_IDF_EXPORT="/home/olwhistle/dockerNow/esp32/ESP-IDF/esp-idf-v6.0.1/export.sh"

if [ -f "$ESP_IDF_EXPORT" ]; then
    # 清理 PATH：保留 Linux 基本路径，NVM 放最前面确保新版 Node.js
    CLEAN_PATH="/root/.nvm/versions/node/v24.16.0/bin:/root/.local/bin:/root/.espressif/python_env/idf6.0_py3.10_env/bin:/root/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:/root/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    export PATH="$CLEAN_PATH"

    source "$ESP_IDF_EXPORT" 2>/dev/null || true
else
    echo -e "${RED}Error: ESP-IDF export.sh not found at $ESP_IDF_EXPORT${NC}"
    return 1
fi

# ---- 项目脚本加入 PATH：scripts/ 下脚本任意目录直接用名字调用 ----
export OVS_PROJECT_ROOT="$_OVS_ROOT"
case ":$PATH:" in
    *":$OVS_PROJECT_ROOT/scripts:"*) ;;          # 已存在，避免重复追加
    *) export PATH="$PATH:$OVS_PROJECT_ROOT/scripts" ;;
esac

# ---- 常用变量（脚本间共享，避免各写一遍） ----
export OVS_SERIAL_PORT="${OVS_SERIAL_PORT:-/dev/ttyACM0}"
export OVS_HOST="${OVS_HOST:-ovs.local}"        # ota_push.sh 会用到

cd "$OVS_PROJECT_ROOT"
unset _OVS_ENV_DIR _OVS_ROOT

echo -e "${GREEN}OVS 环境就绪: $OVS_PROJECT_ROOT${NC}"
echo -e "  idf.py build / flash / monitor   直接可用"
echo -e "  mybuild.sh / ota_push.sh / ovs_release / monitor.sh ... 直接可用 (scripts/)"
