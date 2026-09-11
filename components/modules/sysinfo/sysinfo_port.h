/**
 * @file sysinfo_port.h
 * @brief 系统信息平台移植层（IP 栈/MAC/固件版本，ESP=esp_netif / PC=stub）
 */

#ifndef SYSINFO_PORT_H
#define SYSINFO_PORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 取当前接口的 IPv4 信息（点分十进制）；sta 模式取 STA 接口，否则 AP 接口 */
bool sysinfo_port_get_ip_stack(bool sta, char* ip, int ip_cap,
                               char* netmask, int nm_cap,
                               char* gw, int gw_cap);

/** 取 WiFi MAC（"AA:BB:CC:DD:EE:FF"） */
bool sysinfo_port_get_mac(char* mac, int cap);

/** 取固件版本字符串 */
void sysinfo_port_get_fw_version(char* out, int cap);

/** 开机秒数 */
uint32_t sysinfo_port_uptime_sec(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSINFO_PORT_H */
