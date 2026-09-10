/** 传感器快照 —— 契约 I5：GET /api/sensor */
import { request } from './client'

export function getSensorSnapshot() {
  return request('GET', '/api/sensor', { timeout: 8000 })
}
