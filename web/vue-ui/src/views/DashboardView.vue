<template>
  <div class="page">
    <div class="page-head">
      <h2 class="page-title">{{ t('WEB_DASH_TITLE') }}</h2>
      <span class="time-chip" :class="{ dim: time && !time.synced }">
        <AppIcon name="clock" :size="14" />
        <span>{{ time ? time.text : t('WEB_UNKNOWN') }}</span>
      </span>
    </div>

    <!-- 温湿度卡：30s 轮询 + 手动刷新 -->
    <BaseCard :title="t('WEB_SENSOR_CARD')" icon="thermo">
      <template #extra>
        <button class="icon-btn" :disabled="sensorLoading" @click="sensor.refresh()">
          <AppIcon name="refresh" :size="16" :class="{ spin: sensorLoading }" />
        </button>
      </template>

      <div v-if="sensorData" class="sensor-grid">
        <div class="sensor-item">
          <div class="sensor-value">
            {{ sensorData.valid ? sensorData.temp_c.toFixed(1) : t('WEB_UNKNOWN') }}<small>°C</small>
          </div>
          <div class="sensor-label">{{ t('HOME_TEMP') }}</div>
        </div>
        <div class="sensor-divider"></div>
        <div class="sensor-item">
          <div class="sensor-value">
            {{ sensorData.valid ? sensorData.humi_p.toFixed(1) : t('WEB_UNKNOWN') }}<small>%RH</small>
          </div>
          <div class="sensor-label">{{ t('HOME_HUMI') }}</div>
        </div>
      </div>

      <EmptyState v-if="sensorData && !sensorData.valid" :text="t('SENSOR_NO_DATA')" icon="alert" />
      <p class="sensor-age" v-if="sensorData && sensorData.valid && sensorUpdated">
        {{ t('WEB_SENSOR_AGE', { n: ageSec }) }}
      </p>
      <p class="load-error" v-if="sensorError">{{ sensorErrorText }}</p>
    </BaseCard>

    <!-- 网络模式与配置卡 -->
    <BaseCard :title="t('WEB_NET_CARD')" icon="wifi">
      <template #extra>
        <button class="link-btn" @click="navigate('/network')">
          {{ t('WEB_GO_NET') }}
          <AppIcon name="chevron" :size="14" />
        </button>
      </template>

      <div class="mode-line" v-if="netData">
        <StatusDot :status="modeDot" />
        <span class="mode-label">{{ modeText }}</span>
        <span class="mode-chip warn" v-if="netData.switching">{{ t('NET_SWITCHING') }}</span>
      </div>

      <template v-if="netData">
        <InfoRow :label="t('NET_SSID')" :value="netData.ssid || t('WEB_UNKNOWN')" />
        <InfoRow :label="t('NET_IP')" :value="netData.ip" mono />
        <InfoRow :label="t('NET_NETMASK')" :value="netData.netmask" mono />
        <InfoRow :label="t('NET_GW')" :value="netData.gw" mono />
        <InfoRow :label="t('NET_MAC')" :value="netData.mac" mono />
        <InfoRow :label="t('NET_RSSI')" :value="rssiText" />
      </template>
      <p class="load-error" v-if="netError && !netData">{{ netErrorText }}</p>
    </BaseCard>
  </div>
</template>

<script setup>
import { computed, ref, onUnmounted } from 'vue'
import { usePolling } from '../composables/usePolling'
import { getSensorSnapshot } from '../api/sensor'
import { getNetInfo } from '../api/net'
import { getDeviceTime } from '../api/system'
import { errorKey, errorParams } from '../api/client'
import { t } from '../i18n'
import { useRouter } from '../router'
import BaseCard from '../components/BaseCard.vue'
import InfoRow from '../components/InfoRow.vue'
import StatusDot from '../components/StatusDot.vue'
import EmptyState from '../components/EmptyState.vue'
import AppIcon from '../components/AppIcon.vue'

const { navigate } = useRouter()

// --- 温湿度：30s 轮询 + 手动刷新 ---
const sensorData = ref(null)
const sensor = usePolling(async () => {
  sensorData.value = await getSensorSnapshot()
}, { intervalMs: 30000 })
const sensorLoading = computed(() => sensor.loading.value)
const sensorError = computed(() => sensor.error.value)
const sensorUpdated = computed(() => sensor.lastUpdated.value)

// 采样时间随刷新推进，每 5s 重算相对秒数
const tick = ref(0)
const tickTimer = setInterval(() => { tick.value++ }, 5000)
onUnmounted(() => clearInterval(tickTimer))
const ageSec = computed(() => {
  void tick.value
  return Math.max(0, Math.round((Date.now() - sensor.lastUpdated.value) / 1000))
})

// --- 网络信息：30s 轮询 ---
const netData = ref(null)
const net = usePolling(async () => {
  netData.value = await getNetInfo()
}, { intervalMs: 30000 })
const netError = computed(() => net.error.value)

// --- 设备时间：30s 轮询 ---
const time = ref(null)
usePolling(async () => {
  time.value = await getDeviceTime()
}, { intervalMs: 30000 })

const modeText = computed(() => {
  const m = netData.value?.mode
  return m === 'sta' ? t('HOME_NET_STA')
    : m === 'ap' ? t('HOME_NET_AP') : t('HOME_NET_OFF')
})

const modeDot = computed(() => {
  const m = netData.value?.mode
  if (netData.value?.switching) return 'warn'
  return m === 'sta' || m === 'ap' ? 'ok' : 'off'
})

const rssiText = computed(() =>
  netData.value && netData.value.mode === 'sta'
    ? `${netData.value.rssi} dBm` : t('WEB_UNKNOWN'))

const sensorErrorText = computed(() => {
  const e = sensor.error.value
  return e ? t(errorKey(e), errorParams(e)) : ''
})
const netErrorText = computed(() => {
  const e = net.error.value
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

.time-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 12px;
  background: var(--ovs-card-bg);
  border-radius: 999px;
  box-shadow: var(--ovs-shadow);
  font-size: 13px;
  font-weight: 600;
  color: var(--ovs-primary);
  font-variant-numeric: tabular-nums;
}

.time-chip.dim {
  color: var(--ovs-text-3);
}

.sensor-grid {
  display: flex;
  align-items: center;
  justify-content: space-around;
  padding: 10px 0 6px;
}

.sensor-item {
  text-align: center;
}

.sensor-value {
  font-size: 34px;
  font-weight: 700;
  color: var(--ovs-primary);
  font-variant-numeric: tabular-nums;
  line-height: 1.2;
}

.sensor-value small {
  font-size: 15px;
  font-weight: 600;
  color: var(--ovs-text-3);
  margin-left: 2px;
}

.sensor-label {
  margin-top: 2px;
  font-size: 12px;
  color: var(--ovs-text-2);
}

.sensor-divider {
  width: 1px;
  height: 48px;
  background: var(--ovs-divider);
}

.sensor-age {
  margin-top: 8px;
  text-align: center;
  font-size: 12px;
  color: var(--ovs-text-3);
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
  transition: background 0.15s;
}

.icon-btn:hover:not(:disabled) {
  background: #DDE8FF;
}

.icon-btn:disabled {
  opacity: 0.6;
}

.link-btn {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  border: none;
  background: none;
  color: var(--ovs-primary);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  padding: 4px;
}

.load-error {
  margin-top: 10px;
  font-size: 12.5px;
  color: var(--ovs-danger);
  text-align: center;
}
</style>
