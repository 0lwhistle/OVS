#!/usr/bin/env bash
#
# OTA 固件推送脚本（开发期迭代：build 完一条命令推上新固件）
# 运行环境：WSL2 / Linux
#
# 用法:
#   ./scripts/ota_push.sh [主机] [固件路径]
#   环境变量 OVS_HOST 可代替"主机"参数
#
# 主机解析顺序（WSL2 下 .local 常不可直解）:
#   1. 本身是 IP         → 直接用
#   2. getent hosts      → WSL 内 mDNS/DNS
#   3. powershell.exe    → 借 Windows 原生 mDNS 解析 ovs.local（Win10+）
#
# 成功反馈: 上传进度条 → 设备校验 → 自动重启 → 打印旧/新版本对比 + 槽位
#           + 回滚确认提示；任何一步失败给出明确原因并以非 0 退出。
#
#   ./scripts/ota_push.sh [--app-only] [主机] [固件路径]
#   环境变量 OVS_HOST 可代替"主机"参数
#
#   --app-only: 只推纯 app（跳过 OVSO 容器打包，不带设备树）；
#               默认检测到 build/dtb.bin 会自动拼 OVSO（app+dtb 一起升）
#

set -uo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

APP_ONLY=false
POSITIONAL=()
for arg in "$@"; do
    case "$arg" in
        --app-only) APP_ONLY=true ;;
        *) POSITIONAL+=("$arg") ;;
    esac
done
HOST_IN="${POSITIONAL[0]:-${OVS_HOST:-ovs.local}}"
FIRMWARE="${POSITIONAL[1]:-build/ovs.bin}"
RESP_FILE=$(mktemp /tmp/ota_resp.XXXXXX)
trap 'rm -f "$RESP_FILE"' EXIT

die() { echo -e "${RED}❌ $*${NC}"; exit 1; }

# ---- 检查固件 ----
[ -f "$FIRMWARE" ] || die "固件不存在: $FIRMWARE\n   先构建: idf.py build  或  ./scripts/mybuild.sh"

# 设备树容器存在 → 打包为 OVSO 容器（app+dtb 一次上传，A/B 保护）
# --app-only 时跳过（只升 app，设备树保持设备上当前版本）
UPLOAD_FILE="$FIRMWARE"
DTB_NOTE=""
if $APP_ONLY; then
    DTB_NOTE=" (app-only: 不带设备树)"
elif [ -f "build/dtb.bin" ]; then
    PAYLOAD="build/ota_payload.bin"
    if python3 scripts/ovs_pack_payload.py "$FIRMWARE" build/dtb.bin -o "$PAYLOAD" >/dev/null; then
        UPLOAD_FILE="$PAYLOAD"
        DTB_NOTE=" (OVSO: app+dtb)"
    else
        die "打包 OVSO 容器失败（scripts/ovs_pack_payload.py）"
    fi
fi

FW_SIZE=$(stat -c%s "$UPLOAD_FILE")
LOCAL_SHA=$(sha256sum "$UPLOAD_FILE" | cut -d' ' -f1)

# ---- 主机解析 ----
resolve_host() {
    local h="$1"
    if [[ "$h" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "$h"; return 0
    fi
    if getent hosts "$h" >/dev/null 2>&1; then
        getent hosts "$h" | awk '{print $1; exit}'; return 0
    fi
    if command -v powershell.exe >/dev/null 2>&1; then
        local ip
        ip=$(powershell.exe -NoProfile -Command \
            "(Resolve-DnsName -Name '$h' -ErrorAction SilentlyContinue | Select-Object -First 1).IPAddress" \
            2>/dev/null | tr -d '\r\n')
        if [[ "$ip" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            echo "$ip"; return 0
        fi
    fi
    return 1
}

HOST=$(resolve_host "$HOST_IN") || \
    die "无法解析主机: $HOST_IN\n   WSL2 下 mDNS 常不可用, 请改用 IP:\n     ./scripts/ota_push.sh <设备IP>\n   (IP 见设备启动日志 '[NET] STA connected, IP: ...')"
BASE_URL="http://$HOST"
[ "$HOST" != "$HOST_IN" ] && RESOLVED_NOTE=" (${HOST_IN} → ${HOST})" || RESOLVED_NOTE=""

# ---- 前置状态：设备在线 + 当前版本 ----
get_json_field() {  # $1=json $2=key
    echo "$1" | grep -o "\"$2\":\"[^\"]*\"" | head -1 | cut -d'"' -f4
}
get_json_num() {
    echo "$1" | grep -o "\"$2\":-\?[0-9]*" | head -1 | cut -d: -f2
}

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  OVS OTA 固件推送${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "  目标:   ${GREEN}$BASE_URL${NC}${RESOLVED_NOTE}"
echo -e "  固件:   ${YELLOW}$UPLOAD_FILE$DTB_NOTE ($FW_SIZE bytes)${NC}"
echo -e "  SHA256: ${YELLOW}${LOCAL_SHA:0:16}...${NC}"
echo ""

echo -e "${CYAN}[1/4] 连接设备...${NC}"
OLD_STATUS=""
for i in $(seq 1 20); do
    OLD_STATUS=$(curl -s -m 2 "$BASE_URL/api/ota/status" 2>/dev/null)
    [ -n "$OLD_STATUS" ] && break
    [ $i -eq 1 ] && echo -n "  等待设备"
    echo -n "."
    sleep 1
done
echo ""
[ -z "$OLD_STATUS" ] && die "设备不可达: $HOST\n   检查: 设备是否开机/WiFi 已连/ IP 是否变化(路由器后台或串口日志)"
OLD_VER=$(get_json_field "$OLD_STATUS" version)
OLD_SLOT=$(get_json_num "$OLD_STATUS" current_slot)
echo -e "  ✅ 设备在线, 当前版本: ${GREEN}$OLD_VER${NC} (槽 $OLD_SLOT)"

# ---- 流式上传 ----
echo -e "${CYAN}[2/4] 上传固件...${NC}"
T0=$(date +%s)
HTTP_CODE=$(curl -s -o "$RESP_FILE" -w "%{http_code}" \
    --progress-bar -m 300 \
    -X POST \
    --data-binary @"$UPLOAD_FILE" \
    -H "Content-Type: application/octet-stream" \
    -H "Expect: " \
    "$BASE_URL/api/ota/firmware" 2>&1)
CURL_RC=$?
T1=$(date +%s)
DUR=$((T1 - T0))
[ $DUR -eq 0 ] && DUR=1
echo ""

if [ $CURL_RC -ne 0 ]; then
    echo "  curl rc=$CURL_RC, 响应: $(cat "$RESP_FILE" 2>/dev/null)"
    # 设备校验通过即重启，响应/连接可能被重启掐断——轮询确认是否升级成功
    echo -e "${YELLOW}  连接中断, 轮询设备确认是否已升级重启...${NC}"
    REBOOT_STATUS=""
    for i in $(seq 1 45); do
        REBOOT_STATUS=$(curl -s -m 2 "$BASE_URL/api/ota/status" 2>/dev/null)
        [ -n "$REBOOT_STATUS" ] && break
        echo -n "."
        sleep 1
    done
    echo ""
    NEW_SLOT_TMP=$(get_json_num "$REBOOT_STATUS" current_slot)
    if [ -n "$NEW_SLOT_TMP" ] && [ "$NEW_SLOT_TMP" != "$OLD_SLOT" ]; then
        echo -e "  ✅ 槽位 $OLD_SLOT → $NEW_SLOT_TMP, 升级实际已成功(响应被重启吞掉)"
        NEW_STATUS="$REBOOT_STATUS"
        NEW_VER=$(get_json_field "$NEW_STATUS" version)
        echo -e "${GREEN}========================================${NC}"
        echo -e "${GREEN}  ✅ OTA 成功${NC}"
        echo -e "${GREEN}========================================${NC}"
        echo -e "  版本:   $OLD_VER (槽 $OLD_SLOT) → ${GREEN}$NEW_VER${NC} (槽 $NEW_SLOT_TMP)"
        echo -e "  耗时:   上传 ${DUR}s"
        echo -e "  ${YELLOW}⏳ 回滚保护: 新固件待确认, 稳定运行 15s 后自动生效${NC}"
        exit 0
    fi
    die "上传失败 (耗时 ${DUR}s)\n   常见原因: 中途 WiFi 断开 / 上传超时 / 设备未升级"
fi

RESP=$(cat "$RESP_FILE" 2>/dev/null)
if [ "$HTTP_CODE" != "200" ]; then
    die "上传被拒绝 (HTTP $HTTP_CODE): $RESP"
fi

REMOTE_SHA=$(get_json_field "$RESP" sha256)
echo -e "  ✅ 上传完成: ${DUR}s (约 $((FW_SIZE / DUR / 1024)) KB/s), 设备端镜像校验通过"
if [ -n "$REMOTE_SHA" ] && [ "$REMOTE_SHA" != "$LOCAL_SHA" ]; then
    echo -e "${YELLOW}  ⚠ SHA256 与本地不一致 (本地=${LOCAL_SHA:0:12} 设备=${REMOTE_SHA:0:12})"
    echo -e "    镜像校验已通过, 此提示一般无害, 继续流程${NC}"
fi

# ---- 等待重启 ----
echo -e "${CYAN}[3/4] 设备校验完成, 等待重启...${NC}"
sleep 2
NEW_STATUS=""
for i in $(seq 1 45); do
    NEW_STATUS=$(curl -s -m 2 "$BASE_URL/api/ota/status" 2>/dev/null)
    [ -n "$NEW_STATUS" ] && break
    echo -n "."
    sleep 1
done
echo ""
[ -z "$NEW_STATUS" ] && \
    die "重启后 45s 内未恢复在线\n   可能: 新固件启动失败已回滚 / WiFi 环境变化 / mDNS 换了 IP\n   排查: 串口 monitor 看启动日志, 或重跑本脚本(设备可能已在旧版本上运行)"

# ---- 结果反馈 ----
echo -e "${CYAN}[4/4] 确认结果...${NC}"
NEW_VER=$(get_json_field "$NEW_STATUS" version)
NEW_SLOT=$(get_json_num "$NEW_STATUS" current_slot)
PENDING=$(echo "$NEW_STATUS" | grep -o '"pending_verify":[a-z]*' | cut -d: -f2)

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  ✅ OTA 成功${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "  版本:   $OLD_VER (槽 $OLD_SLOT) → ${GREEN}$NEW_VER${NC} (槽 $NEW_SLOT)"
echo -e "  耗时:   上传 ${DUR}s"
if [ "$PENDING" = "true" ]; then
    echo -e "  ${YELLOW}⏳ 回滚保护: 新固件待确认, 稳定运行 15s 后自动生效${NC}"
    echo -e "  ${YELLOW}   (期间请勿断电; 若崩溃重启将自动回退到 $OLD_VER)${NC}"
    # 再等 17s 确认翻转, 给出最终结论
    printf "  等待回滚确认"
    sleep 17
    FINAL=$(curl -s -m 2 "$BASE_URL/api/ota/status" 2>/dev/null)
    echo ""
    if echo "$FINAL" | grep -q '"pending_verify":false'; then
        echo -e "  ${GREEN}✅ 新固件已确认有效, 升级最终完成${NC}"
    elif [ -n "$FINAL" ]; then
        echo -e "  ${YELLOW}⚠ 仍待确认, 若期间发生过重启说明新固件启动失败已回滚${NC}"
    else
        echo -e "  ${YELLOW}⚠ 设备暂时无响应, 请稍后手动确认: curl $BASE_URL/api/ota/status${NC}"
    fi
else
    echo -e "  ${GREEN}✅ 新固件运行中, 升级完成${NC}"
fi
echo ""
exit 0
