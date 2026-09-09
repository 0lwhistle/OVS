#!/usr/bin/env bash
# 构建（并可选运行）OVS PC 模拟器
# 用法: sim_build.sh [--run]
set -euo pipefail
cd "$(dirname "$0")/.."

if ! pkg-config --exists sdl2 2>/dev/null; then
    echo "❌ 缺少 SDL2 开发库，请先安装: sudo apt install libsdl2-dev"
    exit 1
fi

echo "==> CMake configure (build-sim/)"
cmake -S sim -B build-sim -DCMAKE_BUILD_TYPE=Release

echo "==> Building"
cmake --build build-sim -j"$(nproc)"

echo "✅ 构建完成: ./build-sim/ovs_sim (320x240 @2x 窗口)"
if [ "${1:-}" = "--run" ]; then
    exec ./build-sim/ovs_sim
fi
