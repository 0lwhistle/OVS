/**
 * @file net_mgr_mock.c
 * @brief net_mgr_get_status 的 PC 测试替身（test_hub 专用）
 *
 * ovs_tests 不编译真实 net_mgr（依赖 esp_wifi）；此处提供同名符号，
 * 返回可控的静态状态供 sysinfo 只读查询测试。
 */

#include "net_mgr.h"

#include <stdio.h>
#include <string.h>

static net_status_t s_mock_status = {
    .mode = NET_MODE_STA,
    .state = NET_STATE_CONNECTED,
    .ssid = "wifi2.4g",
    .ip = "192.168.2.111",
    .rssi = -52,
    .sta_configured = true,
    .switching = false,
};

void net_mgr_mock_set_online(bool online) {
    if (online) {
        s_mock_status.mode = NET_MODE_STA;
        s_mock_status.state = NET_STATE_CONNECTED;
        snprintf(s_mock_status.ssid, sizeof(s_mock_status.ssid), "%s", "wifi2.4g");
        snprintf(s_mock_status.ip, sizeof(s_mock_status.ip), "%s", "192.168.2.111");
        s_mock_status.rssi = -52;
    } else {
        s_mock_status.mode = NET_MODE_AP;
        s_mock_status.state = NET_STATE_AP_RUNNING;
        snprintf(s_mock_status.ssid, sizeof(s_mock_status.ssid), "%s", "OVS-Desk");
        snprintf(s_mock_status.ip, sizeof(s_mock_status.ip), "%s", "192.168.4.1");
        s_mock_status.rssi = 0;
    }
}

net_err_t net_mgr_get_status(net_status_t* out) {
    if (!out) {
        return NET_ERR_INVALID_PARAM;
    }
    *out = s_mock_status;
    return NET_OK;
}
