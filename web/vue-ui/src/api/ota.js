/** OTA 升级 —— 复用既有 /api/ota/* 端点（后端不改） */
import { request, uploadFirmware } from './client'

/**
 * GET /api/ota/status →
 * {state:'idle|receiving|verifying|done|failed', received, expected,
 *  current_slot, target_slot, pending_verify, dtb_slot,
 *  version, project, sha256}
 */
export function getOtaStatus() {
  return request('GET', '/api/ota/status', { timeout: 8000 })
}

/**
 * POST /api/ota/firmware —— 流式上传（调用方先完成前端合法性校验）
 * @param {File} file
 * @param {(pct:number)=>void} [onProgress]
 */
export function uploadFirmwareFile(file, onProgress) {
  return uploadFirmware(file, onProgress)
}
