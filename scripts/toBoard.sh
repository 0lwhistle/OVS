#!/usr/bin/env bash
#
# 一键构建并烧录
#
# 用法: bash ./scripts/toBoard.sh [串口]
#

# 确保使用 bash 执行
if [ -z "$BASH_VERSION" ]; then
    echo "Error: This script must be run with bash, not sh"
    echo "Usage: bash ./scripts/toBoard.sh"
    exit 1
fi

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/mybuild.sh"
"$SCRIPT_DIR/burn.sh" "$@"
