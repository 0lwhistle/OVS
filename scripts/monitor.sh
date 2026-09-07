
# 确保使用 bash 执行
if [ -z "$BASH_VERSION" ]; then
    echo "Error: This script must be run with bash, not sh"
    echo "Usage: bash ./scripts/burn.sh"
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

# ESP-IDF 环境
echo -e "${YELLOW}Activating ESP-IDF...${NC}"
ESP_IDF_EXPORT="/home/olwhistle/dockerNow/esp32/ESP-IDF/esp-idf-v6.0.1/export.sh"

if [ -f "$ESP_IDF_EXPORT" ]; then
    # 清理 PATH 中的特殊字符路径
    CLEAN_PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/root/.espressif:/root/.local/bin:/root/.nvm/versions/node/v24.16.0/bin:/snap/bin"
    export PATH="$CLEAN_PATH"
    
    source "$ESP_IDF_EXPORT" 2>/dev/null || {
        echo -e "${YELLOW}Warning: ESP-IDF activation had minor issues, continuing...${NC}"
    }
else
    echo -e "${YELLOW}Warning: ESP-IDF export.sh not found at $ESP_IDF_EXPORT${NC}"
fi

# 串口参数
SERIAL_PORT="${1:-/dev/ttyACM0}"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  OVS Flash Script${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo "  Port: $SERIAL_PORT"
echo ""

idf.py -p "$SERIAL_PORT" monitor
