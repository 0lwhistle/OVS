<template>
  <div class="page">
    <div class="page-head">
      <h2 class="page-title">{{ t('WEB_FW_TITLE') }}</h2>
    </div>

    <!-- 当前固件 -->
    <BaseCard :title="t('WEB_FW_CURRENT')" icon="chip">
      <template #extra>
        <button class="icon-btn" :disabled="otaLoading" @click="ota.refresh()">
          <AppIcon name="refresh" :size="16" :class="{ spin: otaLoading }" />
        </button>
      </template>
      <template v-if="otaStatus">
        <InfoRow :label="t('WEB_FW_VERSION')" :value="otaStatus.version" mono />
        <InfoRow :label="t('WEB_FW_SLOT')"
                 :value="`${otaStatus.current_slot} → ${otaStatus.target_slot}`" mono />
      </template>
      <p class="load-error" v-if="otaError && !otaStatus">{{ otaErrorText }}</p>
    </BaseCard>

    <!-- 上传固件 -->
    <BaseCard :title="t('WEB_FW_UPLOAD')" icon="upload">
      <!-- 结果横幅 -->
      <div v-if="phase.state === 'countdown' || phase.state === 'probing' || phase.state === 'backOnline'"
           class="banner" :class="phase.state === 'backOnline' ? 'ok' : 'info'">
        <AppIcon :name="phase.state === 'backOnline' ? 'check' : 'clock'" :size="18" />
        <div class="banner-body">
          <p class="banner-title">
            {{ phase.state === 'backOnline' ? t('WEB_FW_SUCCESS') : t('WEB_FW_REBOOT') }}
          </p>
          <p class="banner-sub" v-if="phase.state === 'countdown'">
            {{ t('WEB_FW_COUNTDOWN', { s: phase.countdown }) }}
          </p>
          <p class="banner-sub" v-else-if="phase.state === 'probing'">
            {{ t('WEB_FW_WAIT_ONLINE') }}
          </p>
          <p class="banner-sub" v-else>
            {{ t('WEB_FW_BACK_ONLINE') }}
            <template v-if="newVersion"> · {{ t('WEB_FW_NEW_VER') }}: {{ newVersion }}</template>
          </p>
        </div>
        <button v-if="phase.state === 'backOnline'" class="btn btn-primary btn-sm"
                @click="reloadPage">{{ t('BTN_REFRESH') }}</button>
      </div>

      <div v-else>
        <!-- 拖拽/选择区 -->
        <div class="drop-zone" :class="{ dragover, 'has-file': file }"
             @dragover.prevent="dragover = true"
             @dragleave.prevent="dragover = false"
             @drop.prevent="onDrop"
             @click="!file && openPicker()">
          <input ref="fileInput" type="file" accept=".bin" hidden @change="onPick" />
          <template v-if="!file">
            <AppIcon name="upload" :size="30" class="drop-icon" />
            <p class="drop-text">{{ t('WEB_FW_DROP') }}</p>
          </template>
          <template v-else>
            <div class="file-info">
              <span class="file-name">{{ file.name }}</span>
              <span class="file-meta">
                {{ formatSize(file.size) }}
                <template v-if="imageType">
                  · {{ imageType === 'container' ? t('WEB_FW_CONTAINER') : t('WEB_FW_PLAIN') }}
                </template>
              </span>
            </div>
            <button class="clear-btn" @click.stop="clearFile">
              <AppIcon name="close" :size="14" />
            </button>
          </template>
        </div>

        <!-- 校验清单（前端三重合法性检查） -->
        <ul class="check-list" v-if="file">
          <li v-for="c in checks" :key="c.key" :class="c.pass ? 'pass' : 'fail'">
            <AppIcon :name="c.pass ? 'check' : 'close'" :size="14" />
            <span>{{ checkLabel(c) }}</span>
          </li>
        </ul>

        <!-- 错误提示 -->
        <p class="load-error" v-if="validateError">{{ validateError }}</p>

        <!-- 进度 -->
        <div class="progress-box" v-if="phase.state === 'uploading'">
          <ProgressBar :value="progress" />
          <p class="progress-label">
            {{ phase.devicePct == null
              ? t('WEB_FW_UPLOADING', { p: progress })
              : t('WEB_FW_WRITING', { p: progress }) }}
          </p>
        </div>

        <!-- 上传按钮 -->
        <button class="btn btn-primary btn-block" :disabled="!allChecksPass || phase.state === 'uploading'"
                @click="startUpload">
          {{ phase.state === 'uploading' ? t('WEB_FW_CHECKING') : t('WEB_FW_START') }}
        </button>
      </div>

      <p class="hint">{{ t('WEB_FW_TIP') }}</p>
    </BaseCard>
  </div>
</template>

<script setup>
import { computed, ref, watch, onUnmounted } from 'vue'
import { usePolling } from '../composables/usePolling'
import { getOtaStatus } from '../api/ota'
import { request, errorKey, errorParams } from '../api/client'
import { useOta, validateFirmware, formatSize } from '../composables/useOta'
import { t } from '../i18n'
import BaseCard from '../components/BaseCard.vue'
import InfoRow from '../components/InfoRow.vue'
import ProgressBar from '../components/ProgressBar.vue'
import AppIcon from '../components/AppIcon.vue'

// --- 当前固件版本（空闲期 30s 轮询） ---
const otaStatus = ref(null)
const ota = usePolling(async () => {
  otaStatus.value = await getOtaStatus()
}, { intervalMs: 30000 })
const otaLoading = computed(() => ota.loading.value)
const otaError = computed(() => ota.error.value)

// --- 文件选择与校验 ---
const fileInput = ref(null)
const file = ref(null)
const dragover = ref(false)
const checks = ref([])
const imageType = ref(null)
const validateError = ref(null)

const { phase, progress, start, reset } = useOta()

const allChecksPass = computed(() =>
  checks.value.length > 0 && checks.value.every((c) => c.pass))

async function acceptCandidate(candidate) {
  reset()
  file.value = null
  checks.value = []
  imageType.value = null
  validateError.value = null
  try {
    const result = await validateFirmware(candidate)
    file.value = candidate
    imageType.value = result.type
    checks.value = result.checks
  } catch (e) {
    checks.value = e.checks || []
    validateError.value = t(e.reason || 'WEB_FW_ERR_READ')
  }
}

function onPick(e) {
  const f = e.target.files[0]
  if (f) acceptCandidate(f)
  e.target.value = ''
}

function onDrop(e) {
  dragover.value = false
  const f = e.dataTransfer?.files?.[0]
  if (f) acceptCandidate(f)
}

function openPicker() {
  fileInput.value?.click()
}

function clearFile() {
  file.value = null
  checks.value = []
  imageType.value = null
  validateError.value = null
  reset()
}

function checkLabel(c) {
  if (c.key === 'WEB_FW_CHK_SIZE') {
    return t(c.key, { size: formatSize(imageType.value === 'container' ? 0x280000 + 96 + 32 * 1024 : 0x280000) })
  }
  return t(c.key)
}

// --- 升级执行 ---
async function startUpload() {
  if (!file.value || !allChecksPass.value) return
  await start(file.value, imageType.value)
}

/** 设备回归后展示新版本并刷新本地状态 */
const newVersion = ref(null)
watch(() => phase.state, async (s) => {
  if (s === 'backOnline') {
    try {
      const st = await getOtaStatus()
      otaStatus.value = st
      newVersion.value = st?.version || null
    } catch (e) { /* 忽略 */ }
  }
})

function reloadPage() {
  window.location.reload()
}

const otaErrorText = computed(() => {
  const e = ota.error.value
  return e ? t(errorKey(e), errorParams(e)) : ''
})

onUnmounted(() => { /* useOta/usePolling 自清理 */ })
</script>

<style scoped>
.page {
  padding: 16px;
}

.page-head {
  margin-bottom: 14px;
}

.page-title {
  font-size: 20px;
  font-weight: 700;
  color: var(--ovs-text-1);
}

.icon-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 30px;
  height: 30px;
  border: none;
  border-radius: 9px;
  background: var(--ovs-primary-soft);
  color: var(--ovs-primary);
  cursor: pointer;
}

.icon-btn:disabled {
  opacity: 0.6;
}

.drop-zone {
  border: 2px dashed #C7D9FF;
  border-radius: var(--ovs-radius-sm);
  padding: 30px 16px;
  text-align: center;
  cursor: pointer;
  transition: all 0.18s;
  user-select: none;
}

.drop-zone.dragover {
  border-color: var(--ovs-primary);
  background: var(--ovs-primary-soft);
}

.drop-zone.has-file {
  border-style: solid;
  border-color: var(--ovs-primary);
  background: #FAFCFF;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 18px 16px;
}

.drop-icon {
  color: var(--ovs-primary);
}

.drop-text {
  margin-top: 8px;
  font-size: 13.5px;
  color: var(--ovs-text-2);
}

.file-info {
  text-align: left;
  min-width: 0;
  flex: 1;
}

.file-name {
  display: block;
  font-size: 14px;
  font-weight: 600;
  color: var(--ovs-text-1);
  word-break: break-all;
}

.file-meta {
  font-size: 12px;
  color: var(--ovs-text-3);
}

.clear-btn {
  flex-shrink: 0;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border: none;
  border-radius: 50%;
  background: var(--ovs-danger-soft);
  color: var(--ovs-danger);
  cursor: pointer;
}

.check-list {
  list-style: none;
  margin-top: 12px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.check-list li {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  padding: 7px 12px;
  border-radius: 8px;
}

.check-list li.pass {
  background: var(--ovs-success-soft);
  color: var(--ovs-success);
}

.check-list li.fail {
  background: var(--ovs-danger-soft);
  color: var(--ovs-danger);
}

.progress-box {
  margin-top: 14px;
}

.progress-label {
  margin-top: 8px;
  text-align: center;
  font-size: 12.5px;
  color: var(--ovs-text-2);
  font-variant-numeric: tabular-nums;
}

.btn-block {
  width: 100%;
  margin-top: 14px;
}

.btn-sm {
  padding: 7px 14px;
  font-size: 12.5px;
}

.banner {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 14px;
  border-radius: var(--ovs-radius-sm);
}

.banner.info {
  background: var(--ovs-primary-soft);
  color: var(--ovs-primary);
}

.banner.ok {
  background: var(--ovs-success-soft);
  color: var(--ovs-success);
}

.banner-body {
  flex: 1;
}

.banner-title {
  font-size: 14px;
  font-weight: 700;
}

.banner-sub {
  margin-top: 3px;
  font-size: 12.5px;
  opacity: 0.85;
}

.hint {
  margin-top: 12px;
  font-size: 12px;
  color: var(--ovs-text-3);
  line-height: 1.6;
}

.load-error {
  margin-top: 10px;
  font-size: 12.5px;
  color: var(--ovs-danger);
  text-align: center;
}
</style>
