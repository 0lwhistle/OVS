/**
 * @file sysinfo.c
 * @brief 契约 I3 系统/网络信息只读查询实现
 *
 * 数据源：net_mgr_get_status（模式/ssid/rssi/切换态）+ 移植层（IP 栈/
 * MAC/固件版本）。PC 测试以 sysinfo_port.c 的 mock 分支为依赖替身。
 */

#include "sysinfo.h"
#include "sysinfo_port.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "[SYSINFO]";

sysinfo_err_t sysinfo_init(void) {
    LOGI(TAG, "sysinfo ready (read-only net/dev queries)");
    return SYSINFO_OK;
}

sysinfo_err_t sysinfo_net_get(net_info_t* out) {
    if (!out) {
        return SYSINFO_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));

    net_status_t st;
    if (net_mgr_get_status(&st) != NET_OK) {
        LOGW(TAG, "net_mgr_get_status failed");
        return SYSINFO_ERR_FAIL;
    }

    out->mode = st.mode;
    out->switching = st.switching;
    snprintf(out->ssid, sizeof(out->ssid), "%s", st.ssid);
    out->rssi = (st.mode == NET_MODE_STA) ? (int8_t)st.rssi : 0;

    /* IP 栈/MAC：net_mgr 未提供 netmask/gw/mac，经移植层只读查询 */
    if (!sysinfo_port_get_ip_stack(st.mode == NET_MODE_STA,
                                   out->ip, sizeof(out->ip),
                                   out->netmask, sizeof(out->netmask),
                                   out->gw, sizeof(out->gw))) {
        snprintf(out->ip, sizeof(out->ip), "0.0.0.0");
        snprintf(out->netmask, sizeof(out->netmask), "0.0.0.0");
        snprintf(out->gw, sizeof(out->gw), "0.0.0.0");
    }
    if (!sysinfo_port_get_mac(out->mac, sizeof(out->mac))) {
        snprintf(out->mac, sizeof(out->mac), "00:00:00:00:00:00");
    }
    return SYSINFO_OK;
}

sysinfo_err_t sysinfo_device_get(sysinfo_dev_t* out) {
    if (!out) {
        return SYSINFO_ERR_PARAM;
    }
    sysinfo_port_get_fw_version(out->fw_version, sizeof(out->fw_version));
    out->uptime_sec = sysinfo_port_uptime_sec();
    return SYSINFO_OK;
}
