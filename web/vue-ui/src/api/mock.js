/**
 * 设备模拟器（仅 vite dev 生效，见 client.js USE_MOCK）。
 * 响应结构与真实固件端点逐字段一致（契约 I5 + 既有 /api/wifi/*、
 * /api/ota/* 行为），保证 mock 联调与上板零差异切换。
 *
 * 模拟能力：传感器随机游走、Wi-Fi 扫描/连接（含 30s 级切换窗口压缩为
 * 数秒）、模式热切换、OTA 上传进度→校验→重启离线→新版本回归。
 */

class ApiError extends Error {
  constructor(code) { super(code); this.code = code }
}

const rand = (min, max) => min + Math.random() * (max - min)

// ---------------------------------------------------------------------------
// 可变设备状态
// ---------------------------------------------------------------------------
const device = {
  sensor: { temp_c: 26.4, humi_p: 48.2 },
  net: {
    mode: 'ap', ssid: 'OVS-Desk-A4C2', ip: '192.168.4.1',
    netmask: '255.255.255.0', gw: '192.168.4.1',
    mac: '7C:DF:A1:9F:3B:E2', rssi: -52, switching: false,
  },
  wifi: {
    mode: 'ap', state: 'idle', ssid: 'OVS-Desk-A4C2', ip: '192.168.4.1',
    rssi: -52, switching: false, switch_elapsed_sec: 0, sta_configured: true,
  },
  ota: {
    state: 'idle', received: 0, expected: 0,
    current_slot: 0, target_slot: 0, pending_verify: false,
    dtb_slot: 0, version: 'v0.9.3', project: 'ovs', sha256: '',
  },
  timeStart: Date.now(),
  // 重启窗口：rebootUntil 之前所有请求离线
  rebootUntil: 0,
  pendingConnect: null, // {ssid, until}
}

const SCAN_LIST = [
  { ssid: 'Home-WiFi-5G', rssi: -41, authmode: 3 },
  { ssid: 'Home-WiFi', rssi: -48, authmode: 3 },
  { ssid: 'OVS-Desk-A4C2', rssi: -30, authmode: 2 },
  { ssid: 'CoffeeShop_Free', rssi: -67, authmode: 0 },
  { ssid: 'Neighbor_2.4G', rssi: -78, authmode: 4 },
  { ssid: 'ESP32-Test-Lab', rssi: -83, authmode: 3 },
]

function offline() {
  return Date.now() < device.rebootUntil
}

function jsonReply(body) {
  return JSON.parse(JSON.stringify(body))
}

// ---------------------------------------------------------------------------
// OTA 模拟：接收 → 校验 → done → 500ms 后重启（离线窗口 4s）→ 新版本
// ---------------------------------------------------------------------------
let otaTimer = null

function mockUploadFirmware(file, onProgress) {
  if (device.ota.state !== 'idle') {
    return Promise.reject(new ApiError('upload already in progress'))
  }
  device.ota.state = 'receiving'
  device.ota.expected = file.size
  device.ota.received = 0

  return new Promise((resolve, reject) => {
    const started = Date.now()
    const duration = 3500 // 压缩的"上传+写 flash"时长
    clearInterval(otaTimer)
    otaTimer = setInterval(() => {
      const p = Math.min(1, (Date.now() - started) / duration)
      device.ota.received = Math.round(device.ota.expected * p)
      if (onProgress) onProgress(Math.round(p * 100))

      if (p >= 1) {
        clearInterval(otaTimer)
        otaTimer = null
        device.ota.state = 'done'
        device.ota.sha256 = Array.from({ length: 64 }, () =>
          '0123456789abcdef'[Math.floor(Math.random() * 16)]).join('')
        // 模拟 ota_reboot()：500ms 后离线重启，4s 后新固件上线
        setTimeout(() => {
          device.rebootUntil = Date.now() + 4000
          setTimeout(() => {
            device.ota = {
              ...device.ota,
              state: 'idle', received: 0, expected: 0,
              pending_verify: false, sha256: '',
              version: bumpVersion(device.ota.version),
              current_slot: device.ota.current_slot === 0 ? 1 : 0,
            }
          }, 4200)
        }, 500)
        resolve({ status: 'ok', bytes: file.size, message: 'verified, rebooting' })
      }
    }, 80)
  })
}

function bumpVersion(v) {
  const m = String(v).match(/^v(\d+)\.(\d+)\.(\d+)$/)
  if (!m) return 'v0.9.4'
  return `v${m[1]}.${m[2]}.${Number(m[3]) + 1}`
}

// ---------------------------------------------------------------------------
// 请求分发
// ---------------------------------------------------------------------------
/** mock 模式下 body 已是对象（client.js 未做 JSON 序列化），兼容字符串 */
function parseBody(body) {
  if (!body) return {}
  if (typeof body === 'string') { try { return JSON.parse(body) } catch { return {} } }
  return body
}

export async function handleMock(method, path, body) {
  await new Promise((r) => setTimeout(r, 30))
  if (offline() && path !== '/api/ota/status') throw new ApiError('network')

  if (method === 'POST') {
    if (path === '/api/ota/firmware') throw new ApiError('use uploadFirmware()')

    if (path === '/api/wifi/connect') {
      const { ssid, password } = parseBody(body)
      if (!ssid) throw new ApiError('invalid ssid')
      device.wifi.switching = true
      device.pendingConnect = { ssid: String(ssid), hasPw: Boolean(password) }
      setTimeout(() => {
        device.wifi = {
          ...device.wifi, mode: 'sta', state: 'connected',
          ssid: device.pendingConnect.ssid, ip: '192.168.2.111',
          rssi: -55, switching: false,
        }
        device.net = {
          ...device.net, mode: 'sta', ssid: device.pendingConnect.ssid,
          ip: '192.168.2.111', gw: '192.168.2.1', rssi: -55, switching: false,
        }
        device.pendingConnect = null
      }, 3000)
      return { status: 'ok', message: 'switching' }
    }

    if (path === '/api/net/mode') {
      const { mode } = parseBody(body)
      if (!['sta', 'ap', 'off'].includes(mode)) {
        throw new ApiError('need "mode": "sta"|"ap"|"off"')
      }
      device.wifi.switching = true
      setTimeout(() => {
        const apMode = mode === 'ap'
        device.net = {
          ...device.net, mode,
          ssid: apMode ? 'OVS-Desk-A4C2' : device.net.ssid,
          ip: apMode ? '192.168.4.1' : '192.168.2.111',
          gw: apMode ? '192.168.4.1' : '192.168.2.1',
          rssi: apMode ? 0 : -55,
          switching: false,
        }
        device.wifi = {
          ...device.wifi, mode, switching: false,
          state: mode === 'off' ? 'idle' : 'connected',
          ip: device.net.ip, ssid: device.net.ssid, rssi: device.net.rssi,
        }
      }, 2500)
      return { status: 'ok', previous: device.net.mode, mode }
    }

    throw new ApiError('not found')
  }

  // ----- GET -----
  switch (path) {
    case '/api/hello':
      return { message: 'Hello from ESP32!', status: 'ok' }

    case '/api/sensor': {
      // 随机游走，模拟真实采集波动
      device.sensor.temp_c = Math.min(32, Math.max(18,
        device.sensor.temp_c + rand(-0.15, 0.15)))
      device.sensor.humi_p = Math.min(70, Math.max(35,
        device.sensor.humi_p + rand(-0.8, 0.8)))
      return {
        temp_c: Number(device.sensor.temp_c.toFixed(1)),
        humi_p: Number(device.sensor.humi_p.toFixed(1)),
        age_ms: Math.round(rand(800, 2400)),
        valid: true,
      }
    }

    case '/api/net/info':
      return jsonReply(device.net)

    case '/api/time': {
      const ms = Date.now() - device.timeStart
      const d = new Date(ms)
      const hh = String(d.getUTCHours()).padStart(2, '0')
      const mm = String(d.getUTCMinutes()).padStart(2, '0')
      return { synced: false, text: `${hh}:${mm}` }
    }

    case '/api/wifi/status':
      return {
        ...device.wifi,
        switch_elapsed_sec: device.wifi.switching ? 2 : 0,
      }

    case '/api/wifi/scan':
      return { aps: SCAN_LIST, count: SCAN_LIST.length }

    case '/api/ota/status':
      return jsonReply(device.ota)

    default:
      throw new ApiError('not found')
  }
}

export { mockUploadFirmware }
