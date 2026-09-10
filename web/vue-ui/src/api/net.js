/** 网络信息与配网 —— 契约 I5 GET /api/net/info + 既有 wifi/net-mode 端点 */
import { request } from './client'

/** GET /api/net/info → {mode,ssid,ip,netmask,gw,mac,rssi,switching} */
export function getNetInfo() {
  return request('GET', '/api/net/info', { timeout: 8000 })
}

/** GET /api/wifi/status → net_mgr 状态（含 switching/switch_elapsed_sec） */
export function getWifiStatus() {
  return request('GET', '/api/wifi/status', { timeout: 8000 })
}

/** GET /api/wifi/scan → {aps:[{ssid,rssi,authmode}],count}（扫描耗时较长） */
export function scanWifi() {
  return request('GET', '/api/wifi/scan', { timeout: 20000 })
}

/** POST /api/wifi/connect {ssid,password} → 经配网通道提交，返回后进入切换窗口 */
export function connectWifi(ssid, password) {
  return request('POST', '/api/wifi/connect', { body: { ssid, password } })
}

/** POST /api/net/mode {mode:'sta'|'ap'|'off'} → 热切换（免重启） */
export function setNetMode(mode) {
  return request('POST', '/api/net/mode', { body: { mode } })
}
