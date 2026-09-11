/**
 * @file sysinfo_port.c
 * @brief 系统信息平台移植层实现（PC 测试/模拟：静态 mock 值）
 */

#include "sysinfo_port.h"

#include <stdio.h>
#include <string.h>

bool sysinfo_port_get_ip_stack(bool sta, char* ip, int ip_cap,
                               char* netmask, int nm_cap,
                               char* gw, int gw_cap) {
    (void)sta;
    snprintf(ip, ip_cap, "%s", "192.168.2.111");
    snprintf(netmask, nm_cap, "%s", "255.255.255.0");
    snprintf(gw, gw_cap, "%s", "192.168.2.1");
    return true;
}

bool sysinfo_port_get_mac(char* mac, int cap) {
    snprintf(mac, cap, "%s", "AA:BB:CC:DD:EE:FF");
    return true;
}

void sysinfo_port_get_fw_version(char* out, int cap) {
    snprintf(out, cap, "%s", "0.0.0-pc");
}

uint32_t sysinfo_port_uptime_sec(void) {
    return 0;
}
