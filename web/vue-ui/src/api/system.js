/** 系统信息 —— 契约 I5：GET /api/time */
import { request } from './client'

/** GET /api/time → {synced:boolean, text:'HH:MM'} */
export function getDeviceTime() {
  return request('GET', '/api/time', { timeout: 8000 })
}
