/**
 * api 层统一入口：集中封装 fetch，统一错误处理与错误码判型。
 *
 * 响应归一化：
 *   - 2xx → 返回解析后的 JSON（后端均为裸 JSON，无 {code,data} 信封）
 *   - 非 2xx / 网络失败 / 超时 → 抛 ApiError{status, code}
 *     code 取响应体 error 字段（如 "invalid ssid"），否则 http_<status> /
 *     timeout / network，供视图映射 i18n 文案。
 *
 * Mock：开发服务器（vite dev）默认启用设备模拟器（api/mock.js），
 * 设 VITE_USE_DEVICE=1 时直连真实设备（经 vite 代理 /api）。
 */
import { handleMock, mockUploadFirmware } from './mock'

export const USE_MOCK = import.meta.env.DEV && import.meta.env.VITE_USE_DEVICE !== '1'

const DEFAULT_TIMEOUT = 10000

export class ApiError extends Error {
  constructor(code, { status = 0 } = {}) {
    super(code)
    this.name = 'ApiError'
    this.status = status
    this.code = code
  }
}

/** 错误码 → i18n 键（视图统一走这里展示错误） */
export function errorKey(e) {
  if (!(e instanceof ApiError)) return 'WEB_ERR_NETWORK'
  if (e.code === 'timeout') return 'WEB_ERR_TIMEOUT'
  if (e.status >= 400) return 'WEB_ERR_SERVER'
  return 'WEB_ERR_NETWORK'
}

/** ApiError → 文案插值参数（WEB_ERR_SERVER 的 {msg}） */
export function errorParams(e) {
  return e instanceof ApiError && e.status >= 400 ? { msg: e.code } : undefined
}

/**
 * 发起请求
 * @param {'GET'|'POST'} method
 * @param {string} path
 * @param {object} [opts]
 * @param {string|Blob} [opts.body] 请求体；JSON 对象自动序列化
 * @param {number} [opts.timeout] 毫秒
 * @returns {Promise<any>} 解析后的 JSON
 */
export async function request(method, path, opts = {}) {
  const { body, timeout = DEFAULT_TIMEOUT } = opts

  if (USE_MOCK) {
    await new Promise((r) => setTimeout(r, 120))
    return handleMock(method, path, body)
  }

  const payload = (body !== null && typeof body === 'object' && !(body instanceof Blob))
    ? JSON.stringify(body)
    : body

  const ctrl = new AbortController()
  const timer = setTimeout(() => ctrl.abort(), timeout)
  try {
    const res = await fetch(path, {
      method,
      body: payload,
      signal: ctrl.signal,
      headers: payload instanceof Blob
        ? { 'Content-Type': 'application/octet-stream' }
        : payload !== undefined
          ? { 'Content-Type': 'application/json' }
          : undefined,
    })
    const text = await res.text()
    let data = null
    try { data = text ? JSON.parse(text) : null } catch { data = null }
    if (!res.ok) {
      throw new ApiError(data?.error || `http_${res.status}`, { status: res.status })
    }
    return data
  } catch (e) {
    if (e instanceof ApiError) throw e
    throw new ApiError(e.name === 'AbortError' ? 'timeout' : 'network')
  } finally {
    clearTimeout(timer)
  }
}

/**
 * 固件上传（XMLHttpRequest 以获得上传进度；mock 态走模拟器）
 * @param {File} file
 * @param {(pct:number)=>void} onProgress 0~100
 * @param {number} timeoutMs 总超时（大文件放宽）
 * @returns {Promise<object>} 设备响应 JSON
 */
export function uploadFirmware(file, onProgress, timeoutMs = 120000) {
  if (USE_MOCK) return mockUploadFirmware(file, onProgress)
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest()
    xhr.open('POST', '/api/ota/firmware')
    xhr.timeout = timeoutMs
    xhr.setRequestHeader('Content-Type', 'application/octet-stream')
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) {
        onProgress(Math.round((e.loaded / e.total) * 100))
      }
    }
    xhr.onload = () => {
      let data = null
      try { data = JSON.parse(xhr.responseText) } catch { data = null }
      if (xhr.status === 200) return resolve(data)
      reject(new ApiError(data?.error || `http_${xhr.status}`, { status: xhr.status }))
    }
    xhr.onerror = () => reject(new ApiError('network'))
    xhr.ontimeout = () => reject(new ApiError('timeout'))
    xhr.send(file)
  })
}
