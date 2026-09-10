/**
 * OTA 升级状态机（Firmware 页专用）。
 *
 * 流程：idle → validating（前端三重校验）→ uploading（XHR 进度 +
 * 设备侧 /api/ota/status 轮询）→ 200 后设备 500ms 内重启（ota_reboot）
 * → countdown（重启倒计时）→ probing（探测 /api/hello 等待上线）
 * → backOnline（展示新版本）。
 *
 * 前端校验（契约 I5 固件合法性分工）：
 *   1. 扩展名 .bin；
 *   2. 大小 ≤ OTA 分区（纯 app 0x280000；OVSO 容器另加 96B 头 + 32K 树槽）；
 *   3. 魔数：首字节 0xE9（ESP app image）或头 4 字节 "OVSO"（容器）。
 * 任一不合法直接拒绝，不发起上传；后端 ota_begin 长度/SHA256/回滚保护
 * 为第二道防线（web_ota.c 既有逻辑，未改动）。
 */
import { computed, reactive, onUnmounted } from 'vue'
import { request } from '../api/client'
import { uploadFirmwareFile } from '../api/ota'

/** 分区表 ota_0/ota_1 各 0x280000；容器另含 96B 头与 ≤32K 设备树 */
export const OTA_APP_MAX = 0x280000
export const OTA_CONTAINER_MAX = OTA_APP_MAX + 96 + 32 * 1024

const OVSO_MAGIC = [0x4f, 0x56, 0x53, 0x4f] // "OVSO"

export function formatSize(bytes) {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`
}

/** 读取文件头 4 字节判定镜像类型；-1 = 非法 */
async function detectImageType(file) {
  let head
  try {
    head = new Uint8Array(await file.slice(0, 4).arrayBuffer())
  } catch (e) {
    throw new Error('WEB_FW_ERR_READ')
  }
  if (head.length >= 1 && head[0] === 0xe9) return 'app'
  if (head.length >= 4 && OVSO_MAGIC.every((b, i) => head[i] === b)) return 'container'
  return null
}

/**
 * 前端合法性校验。返回 {ok, type, limit, checks}；
 * checks: [{key, pass}]，key 为 i18n 键（WEB_FW_CHK_*）。
 */
export async function validateFirmware(file) {
  const checks = []

  const extOk = /\.bin$/i.test(file.name)
  checks.push({ key: 'WEB_FW_CHK_EXT', pass: extOk })
  if (!extOk) throw { checks, reason: 'WEB_FW_ERR_EXT' }

  let type
  try {
    type = await detectImageType(file)
  } catch (e) {
    throw { checks, reason: e.message }
  }
  const magicOk = type !== null
  checks.push({ key: 'WEB_FW_CHK_MAGIC', pass: magicOk })
  if (!magicOk) throw { checks, reason: 'WEB_FW_ERR_MAGIC' }

  const limit = type === 'container' ? OTA_CONTAINER_MAX : OTA_APP_MAX
  const sizeOk = file.size > 0 && file.size <= limit
  checks.push({ key: 'WEB_FW_CHK_SIZE', pass: sizeOk })
  if (!sizeOk) throw { checks, reason: 'WEB_FW_ERR_SIZE' }

  return { ok: true, type, limit, checks }
}

export function useOta() {
  // idle | uploading | countdown | probing | backOnline
  const phase = reactive({
    state: 'idle',
    uploadPct: 0,        // XHR 发送进度
    devicePct: null,     // 设备侧 received/expected（轮询）
    countdown: 0,
    errorKey: null,
    errorDetail: null,
    result: null,        // {type} 上传的镜像类型
  })

  let pollTimer = null
  let countTimer = null
  let disposed = false

  const progress = computed(() =>
    Math.max(phase.uploadPct, phase.devicePct ?? 0))

  function stopTimers() {
    if (pollTimer) { clearInterval(pollTimer); pollTimer = null }
    if (countTimer) { clearInterval(countTimer); countTimer = null }
  }

  /** 上传期间轮询设备侧写入进度（received/expected） */
  function startDevicePoll() {
    pollTimer = setInterval(async () => {
      try {
        const st = await request('GET', '/api/ota/status', { timeout: 4000 })
        if (st && st.expected > 0) {
          phase.devicePct = Math.min(100,
            Math.round((st.received / st.expected) * 100))
        }
      } catch (e) { /* 状态查询失败不打断上传 */ }
    }, 1000)
  }

  /** 200 响应后：倒计时 → 探测上线 */
  function startRebootWatch() {
    phase.state = 'countdown'
    phase.countdown = 8
    countTimer = setInterval(() => {
      if (disposed) return
      phase.countdown -= 1
      if (phase.countdown <= 0) {
        clearInterval(countTimer)
        countTimer = null
        probeOnline()
      }
    }, 1000)
  }

  async function probeOnline(attempt = 0) {
    if (disposed) return
    phase.state = 'probing'
    try {
      await request('GET', '/api/hello', { timeout: 3000 })
      phase.state = 'backOnline'
    } catch (e) {
      if (attempt < 30) {
        setTimeout(() => probeOnline(attempt + 1), 2000)
      } else {
        phase.state = 'backOnline' // 超时也放行，让用户自行刷新
      }
    }
  }

  /**
   * 执行升级：validateFirmware 由视图先行调用（展示校验清单），
   * 本函数假定 file 已通过校验。
   */
  async function start(file, type) {
    stopTimers()
    phase.state = 'uploading'
    phase.uploadPct = 0
    phase.devicePct = null
    phase.errorKey = null
    phase.errorDetail = null
    phase.result = { type }
    startDevicePoll()
    try {
      await uploadFirmwareFile(file, (pct) => { phase.uploadPct = pct })
      startRebootWatch()
    } catch (e) {
      stopTimers()
      phase.state = 'idle'
      phase.errorKey = 'WEB_FW_FAIL'
      phase.errorDetail = e.code || null
    }
  }

  function reset() {
    stopTimers()
    phase.state = 'idle'
    phase.uploadPct = 0
    phase.devicePct = null
    phase.errorKey = null
    phase.errorDetail = null
    phase.result = null
  }

  onUnmounted(() => { disposed = true; stopTimers() })

  return { phase, progress, start, reset }
}
