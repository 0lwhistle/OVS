<template>
  <div class="page">
    <div class="page-head">
      <h2 class="page-title">{{ t('NET_TITLE') }}</h2>
      <button class="icon-btn" :disabled="statusLoading" @click="status.refresh()">
        <AppIcon name="refresh" :size="16" :class="{ spin: statusLoading }" />
      </button>
    </div>

    <!-- 连接状态 -->
    <BaseCard :title="t('NET_STATUS_CARD')" icon="wifi">
      <div class="mode-line">
        <StatusDot :status="stateDot" />
        <span class="mode-label">{{ stateText }}</span>
        <span class="mode-chip warn" v-if="statusData?.switching">{{ t('NET_SWITCHING') }}</span>
      </div>
      <template v-if="statusData">
        <InfoRow :label="t('NET_SSID')" :value="statusData.ssid || t('WEB_UNKNOWN')" />
        <InfoRow :label="t('NET_IP')" :value="statusData.ip" mono />
        <InfoRow :label="t('NET_RSSI')" :value="rssiText" />
        <InfoRow :label="t('NET_STA_CONFIGURED')"
                 :value="statusData.sta_configured ? t('NET_CONNECTED') : t('NET_DISCONNECTED')" />
      </template>
      <p class="load-error" v-if="statusError && !statusData">{{ statusErrorText }}</p>
    </BaseCard>

    <!-- 模式切换：STA / AP / OFF 热切换 -->
    <BaseCard :title="t('NET_MODE')" icon="chip">
      <div class="seg-group">
        <button v-for="m in MODES" :key="m.value" class="seg-btn"
                :class="{ active: statusData?.mode === m.value, busy: modeBusy }"
                :disabled="modeBusy"
                @click="applyMode(m.value)">
          {{ m.label }}
        </button>
      </div>      <p class="hint">{{ t('NET_MODE_HINT') }}</p>
      <p class="load-error" v-if="modeError">{{ modeErrorText }}</p>
    </BaseCard>

    <!-- 选网连接 -->
    <BaseCard :title="t('NET_SCAN_CARD')" icon="signal">
      <template #extra>
        <button class="btn btn-ghost btn-sm" :disabled="scanning" @click="doScan">
          <AppIcon name="refresh" :size="14" :class="{ spin: scanning }" />
          {{ scanning ? t('NET_SCANNING') : t('NET_SCAN') }}
        </button>
      </template>

      <!-- 连接中遮罩态 -->
      <div v-if="connecting" class="connecting-box">
        <span class="spinner"></span>
        <p>{{ t('NET_CONNECTING') }}</p>
        <p class="hint">{{ connectSsid }}</p>
      </div>

      <template v-else>
        <div v-if="aps.length" class="ap-list">
          <div v-for="ap in aps" :key="ap.ssid" class="ap-item"
               :class="{ selected: selected?.ssid === ap.ssid }"
               @click="selectAp(ap)">
            <div class="ap-main">
              <span class="ap-ssid">{{ ap.ssid }}</span>
              <AppIcon v-if="ap.authmode !== 0" name="lock" :size="14" class="ap-lock" />
            </div>
            <div class="ap-meta">
              <span class="rssi-bars" :class="rssiLevel(ap.rssi)">
                <i></i><i></i><i></i>
              </span>
              <span class="ap-rssi">{{ ap.rssi }} dBm</span>
            </div>
          </div>
        </div>
        <EmptyState v-else :text="t('NET_SCAN_EMPTY')" icon="wifi" />

        <!-- 密码输入（展开） -->
        <div v-if="selected" class="connect-box">
          <p class="connect-title">{{ t('NET_CONNECT_TO', { ssid: selected.ssid }) }}</p>
          <template v-if="selected.authmode !== 0">
            <input class="field" type="password" v-model="password"
                   :placeholder="t('NET_PASSWORD')" maxlength="64" />
          </template>
          <p class="hint" v-else>{{ t('NET_PASSWORD_OPT') }}</p>
          <div class="connect-actions">
            <button class="btn btn-ghost" @click="selected = null">{{ t('BTN_CANCEL') }}</button>
            <button class="btn btn-primary" :disabled="connectBusy" @click="doConnect">
              {{ t('BTN_OK') }}
            </button>
          </div>
          <p class="load-error" v-if="connectError">{{ connectErrorText }}</p>
        </div>
      </template>
    </BaseCard>
  </div>
</template>

<script setup>
import { computed, ref } from 'vue'
import { usePolling } from '../composables/usePolling'
import { getWifiStatus, scanWifi, connectWifi, setNetMode } from '../api/net'
import { errorKey, errorParams } from '../api/client'
import { t } from '../i18n'
import BaseCard from '../components/BaseCard.vue'
import InfoRow from '../components/InfoRow.vue'
import StatusDot from '../components/StatusDot.vue'
import EmptyState from '../components/EmptyState.vue'
import AppIcon from '../components/AppIcon.vue'

const MODES = computed(() => [
  { value: 'sta', label: t('HOME_NET_STA') },
  { value: 'ap', label: t('HOME_NET_AP') },
  { value: 'off', label: t('HOME_NET_OFF') },
])

// --- 连接状态轮询（切换期间加密轮询由 polling 内 hidden 暂停，间隔 4s） ---
const statusData = ref(null)
const status = usePolling(async () => {
  statusData.value = await getWifiStatus()
}, { intervalMs: 4000 })
const statusLoading = computed(() => status.loading.value)
const statusError = computed(() => status.error.value)

// --- 模式切换 ---
const modeBusy = ref(false)
const modeError = ref(null)

async function applyMode(mode) {
  if (modeBusy.value || statusData.value?.mode === mode) return
  modeBusy.value = true
  modeError.value = null
  try {
    await setNetMode(mode)
    // 切换窗口内密集轮询，直到 switching 结束
    const deadline = Date.now() + 35000
    while (Date.now() < deadline) {
      await new Promise((r) => setTimeout(r, 2000))
      const st = await getWifiStatus()
      statusData.value = st
      if (!st.switching) break
    }
  } catch (e) {
    modeError.value = e
  } finally {
    modeBusy.value = false
  }
}

// --- 扫描与连接 ---
const aps = ref([])
const scanning = ref(false)
const selected = ref(null)
const password = ref('')
const connectBusy = ref(false)
const connectError = ref(null)
const connecting = ref(false)
const connectSsid = ref('')

async function doScan() {
  if (scanning.value) return
  scanning.value = true
  try {
    const res = await scanWifi()
    aps.value = (res?.aps || []).sort((a, b) => b.rssi - a.rssi)
  } catch (e) {
    aps.value = []
  } finally {
    scanning.value = false
  }
}

function selectAp(ap) {
  selected.value = ap
  password.value = ''
  connectError.value = null
}

async function doConnect() {
  if (connectBusy.value || !selected.value) return
  connectBusy.value = true
  connectError.value = null
  try {
    await connectWifi(selected.value.ssid, password.value)
    connectSsid.value = selected.value.ssid
    connecting.value = true
    selected.value = null
    // 轮询等待切换完成（后端 30s 回退窗口）
    const deadline = Date.now() + 40000
    while (Date.now() < deadline) {
      await new Promise((r) => setTimeout(r, 2000))
      const st = await getWifiStatus()
      statusData.value = st
      if (!st.switching) {
        if (st.state === 'connected' && st.mode === 'sta') {
          aps.value = []
          break
        }
        if (st.state === 'failed') {
          connectError.value = { code: 'failed' }
          break
        }
      }
    }
  } catch (e) {
    connectError.value = e
  } finally {
    connectBusy.value = false
    connecting.value = false
  }
}

// --- 派生显示 ---
const stateText = computed(() => {
  const s = statusData.value
  if (!s) return t('WEB_UNKNOWN')
  if (s.switching) return t('NET_SWITCHING')
  switch (s.state) {
    case 'connected': return t('NET_CONNECTED')
    case 'connecting': return t('NET_CONNECTING')
    case 'ap_running': return t('HOME_NET_AP')
    case 'failed': return t('NET_CONNECT_FAIL')
    default: return t('NET_DISCONNECTED')
  }
})

const stateDot = computed(() => {
  const s = statusData.value
  if (!s) return 'off'
  if (s.switching) return 'warn'
  if (s.state === 'connected' || s.state === 'ap_running') return 'ok'
  if (s.state === 'connecting') return 'warn'
  return 'off'
})

const rssiText = computed(() =>
  statusData.value && statusData.value.mode === 'sta'
    ? `${statusData.value.rssi} dBm` : t('WEB_UNKNOWN'))

function rssiLevel(rssi) {
  if (rssi >= -55) return 'strong'
  if (rssi >= -75) return 'mid'
  return 'weak'
}

const statusErrorText = computed(() => {
  const e = status.error.value
  return e ? t(errorKey(e), errorParams(e)) : ''
})
const modeErrorText = computed(() => {
  const e = modeError.value
  return e ? t(errorKey(e), errorParams(e)) : ''
})
const connectErrorText = computed(() => {
  const e = connectError.value
  return e ? t(errorKey(e), errorParams(e)) : ''
})
</script>

<style scoped>
.page {
  padding: 16px;
}

.page-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
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
  width: 32px;
  height: 32px;
  border: none;
  border-radius: 10px;
  background: var(--ovs-card-bg);
  box-shadow: var(--ovs-shadow);
  color: var(--ovs-primary);
  cursor: pointer;
}

.icon-btn:disabled {
  opacity: 0.6;
}

.mode-line {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 6px;
}

.mode-label {
  font-size: 14px;
  font-weight: 600;
  color: var(--ovs-text-1);
}

.mode-chip {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 999px;
}

.mode-chip.warn {
  background: var(--ovs-warning-soft);
  color: var(--ovs-warning);
}

.seg-group {
  display: flex;
  background: #F0F3F8;
  border-radius: var(--ovs-radius-sm);
  padding: 3px;
  gap: 3px;
}

.seg-btn {
  flex: 1;
  padding: 9px 6px;
  border: none;
  border-radius: 8px;
  background: transparent;
  font-size: 13px;
  font-weight: 500;
  color: var(--ovs-text-2);
  cursor: pointer;
  transition: all 0.18s;
  white-space: nowrap;
}

.seg-btn.active {
  background: #fff;
  color: var(--ovs-primary);
  font-weight: 600;
  box-shadow: 0 1px 4px rgba(26, 42, 80, 0.12);
}

.seg-btn.busy {
  opacity: 0.55;
}

.hint {
  margin-top: 10px;
  font-size: 12px;
  color: var(--ovs-text-3);
  line-height: 1.6;
}

.btn-sm {
  padding: 7px 12px;
  font-size: 12.5px;
}

.ap-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.ap-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  padding: 12px 14px;
  border: 1.5px solid var(--ovs-divider);
  border-radius: var(--ovs-radius-sm);
  cursor: pointer;
  transition: all 0.15s;
}

.ap-item:hover {
  border-color: #C7D9FF;
}

.ap-item.selected {
  border-color: var(--ovs-primary);
  background: var(--ovs-primary-soft);
}

.ap-main {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.ap-ssid {
  font-size: 14px;
  font-weight: 500;
  color: var(--ovs-text-1);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.ap-lock {
  color: var(--ovs-text-3);
  flex-shrink: 0;
}

.ap-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}

.ap-rssi {
  font-size: 12px;
  color: var(--ovs-text-3);
  font-variant-numeric: tabular-nums;
}

.rssi-bars {
  display: inline-flex;
  align-items: flex-end;
  gap: 2px;
}

.rssi-bars i {
  width: 4px;
  border-radius: 1px;
  background: var(--ovs-divider);
}

.rssi-bars i:nth-child(1) { height: 6px; }
.rssi-bars i:nth-child(2) { height: 10px; }
.rssi-bars i:nth-child(3) { height: 14px; }

.rssi-bars.strong i { background: var(--ovs-success); }
.rssi-bars.mid i { background: var(--ovs-warning); }
.rssi-bars.weak i { background: var(--ovs-danger); }
.rssi-bars.weak i:nth-child(n+2) { background: var(--ovs-divider); }
.rssi-bars.mid i:nth-child(3) { background: var(--ovs-warning); }
.rssi-bars.mid i:nth-child(2) { background: var(--ovs-warning); }

.connect-box {
  margin-top: 14px;
  padding: 14px;
  border-radius: var(--ovs-radius-sm);
  background: #F8FAFE;
  border: 1.5px solid var(--ovs-primary-soft);
}

.connect-title {
  font-size: 13.5px;
  font-weight: 600;
  color: var(--ovs-text-1);
  margin-bottom: 10px;
}

.connect-actions {
  display: flex;
  gap: 10px;
  margin-top: 12px;
}

.connect-actions .btn {
  flex: 1;
}

.connecting-box {
  text-align: center;
  padding: 22px 0;
  color: var(--ovs-text-2);
  font-size: 14px;
}

.connecting-box p {
  margin-top: 4px;
}

.spinner {
  display: inline-block;
  width: 26px;
  height: 26px;
  border: 3px solid var(--ovs-primary-soft);
  border-top-color: var(--ovs-primary);
  border-radius: 50%;
  animation: ovs-spin 0.9s linear infinite;
}

.load-error {
  margin-top: 10px;
  font-size: 12.5px;
  color: var(--ovs-danger);
  text-align: center;
}
</style>
