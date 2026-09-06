<template>
  <div class="wifi-page">
    <!-- 动态背景 -->
    <div class="bg-particles">
      <span v-for="n in 12" :key="n" class="particle" :style="particleStyle(n)"></span>
    </div>

    <!-- 状态头部 -->
    <div class="status-header" :class="headerClass">
      <div class="wifi-icon-wrap">
        <svg class="wifi-icon" viewBox="0 0 24 24" :class="{ scanning: scanning, connected: isConnected }">
          <path d="M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C17.93 4.04 6.07 4.04 1 9zm8 8l3 3 3-3c-1.65-1.66-4.34-1.66-6 0zm-6-6l2 2c2.76-2.76 7.24-2.76 10 0l2-2C13.14 7.14 5.86 7.14 3 11z"/>
        </svg>
      </div>
      <div class="status-text">
        <h2>{{ statusTitle }}</h2>
        <p>{{ statusSubtitle }}</p>
      </div>
      <!-- 切换倒计时 -->
      <div v-if="switchState === 'switching'" class="countdown-ring">
        <svg viewBox="0 0 60 60">
          <circle cx="30" cy="30" r="26" class="countdown-bg"/>
          <circle cx="30" cy="30" r="26" class="countdown-fg"
                  :style="{ strokeDashoffset: countdownOffset }"/>
        </svg>
        <span class="countdown-text">{{ 30 - elapsedSec }}s</span>
      </div>
    </div>

    <!-- 切换进度条 -->
    <div v-if="switchState === 'switching'" class="progress-section">
      <div class="progress-bar">
        <div class="progress-fill" :style="{ width: progressPct + '%' }"></div>
      </div>
      <p class="progress-label">正在连接 <strong>{{ newSsid }}</strong>…</p>
    </div>

    <!-- 操作区 -->
    <div class="action-bar">
      <button class="btn-scan" @click="startScan" :disabled="scanning || switchState === 'switching'">
        <span class="btn-icon" :class="{ spinning: scanning }">⟳</span>
        {{ scanning ? '扫描中…' : '扫描附近网络' }}
      </button>
    </div>

    <!-- AP 列表 -->
    <div class="ap-list" v-if="!scanning && aps.length > 0">
      <div
        v-for="(ap, idx) in aps"
        :key="idx"
        class="ap-item"
        :class="{ selected: selectedIdx === idx }"
        @click="selectAp(idx)"
      >
        <div class="ap-main">
          <div class="ap-signal">
            <span class="signal-bars" :class="'sig-' + signalLevel(ap.rssi)">
              <i></i><i></i><i></i><i></i>
            </span>
          </div>
          <div class="ap-info">
            <span class="ap-ssid">{{ ap.ssid }}</span>
            <span class="ap-meta">
              {{ ap.rssi }} dBm
              <span v-if="ap.authmode > 0" class="lock-icon">🔒</span>
              <span v-else class="lock-icon open">🔓</span>
            </span>
          </div>
          <div class="ap-arrow">›</div>
        </div>

        <!-- 密码输入（选中时展开） -->
        <div v-if="selectedIdx === idx" class="ap-password" @click.stop>
          <input
            v-model="password"
            type="password"
            :placeholder="ap.authmode === 0 ? '开放网络，无需密码' : '输入 Wi-Fi 密码'"
            :disabled="ap.authmode === 0"
            class="pwd-input"
            @keyup.enter="connectAp"
          />
          <button
            class="btn-connect"
            :class="{ pulsing: switchState === 'switching' }"
            :disabled="switchState === 'switching' || (ap.authmode > 0 && !password)"
            @click="connectAp"
          >
            {{ switchState === 'switching' ? '连接中…' : '连接' }}
          </button>
        </div>
      </div>
    </div>

    <!-- 空态 -->
    <div class="empty-state" v-if="!scanning && aps.length === 0 && !scanError">
      <div class="empty-icon">📡</div>
      <p>点击上方按钮扫描附近的 Wi-Fi 网络</p>
    </div>

    <!-- 错误 -->
    <div class="error-state" v-if="scanError">
      <p>{{ scanError }}</p>
    </div>

    <!-- 切换失败提示 -->
    <div v-if="switchState === 'failed'" class="fail-banner">
      <span>⚠️ 连接超时，已回退到原网络 {{ currentSsid }}</span>
      <button @click="dismissFail" class="btn-dismiss">知道了</button>
    </div>

    <!-- 切换成功提示 -->
    <div v-if="switchState === 'connected' && newSsid" class="success-banner">
      <span>✅ 已成功连接到 {{ currentSsid }}</span>
    </div>
  </div>
</template>

<script>
export default {
  data() {
    return {
      aps: [],
      scanning: false,
      scanError: null,
      selectedIdx: -1,
      password: '',

      // 连接状态
      switchState: 'idle',    // idle | scanning | switching | connected | failed
      currentSsid: '',
      newSsid: '',
      elapsedSec: 0,
      rssi: 0,

      // 轮询
      pollTimer: null,
    }
  },
  computed: {
    isConnected() {
      return this.switchState === 'connected' && this.currentSsid
    },
    statusTitle() {
      switch (this.switchState) {
        case 'switching': return '正在切换网络…'
        case 'connected': return this.currentSsid || '已连接'
        case 'failed': return '连接失败'
        default: return this.currentSsid || 'Wi-Fi 配网'
      }
    },
    statusSubtitle() {
      switch (this.switchState) {
        case 'switching': return '请勿关闭设备或离开页面'
        case 'connected': return this.rssi ? `${this.rssi} dBm` : '已连接'
        case 'failed': return '已回退到原网络'
        default: return this.currentSsid ? `${this.rssi} dBm` : '选择网络进行配置'
      }
    },
    headerClass() {
      return {
        'header-switching': this.switchState === 'switching',
        'header-connected': this.switchState === 'connected',
        'header-failed': this.switchState === 'failed',
      }
    },
    progressPct() {
      return Math.min(100, Math.round((this.elapsedSec / 30) * 100))
    },
    countdownOffset() {
      const circumference = 2 * Math.PI * 26  // r=26
      return circumference * (1 - this.elapsedSec / 30)
    }
  },
  methods: {
    signalLevel(rssi) {
      if (rssi >= -50) return 4
      if (rssi >= -60) return 3
      if (rssi >= -70) return 2
      if (rssi >= -80) return 1
      return 0
    },
    particleStyle(n) {
      const size = 2 + Math.random() * 3
      return {
        left: ((n * 37 + 13) % 100) + '%',
        top: ((n * 53 + 7) % 100) + '%',
        width: size + 'px',
        height: size + 'px',
        animationDelay: (n * 0.7) + 's',
        animationDuration: (4 + Math.random() * 4) + 's',
      }
    },
    async startScan() {
      this.scanning = true
      this.scanError = null
      this.selectedIdx = -1
      this.password = ''

      try {
        const res = await fetch('/api/wifi/scan')
        const data = await res.json()
        this.aps = data.aps || []
      } catch (e) {
        this.scanError = '扫描失败：' + e.message
        this.aps = []
      }

      this.scanning = false
    },
    selectAp(idx) {
      this.selectedIdx = this.selectedIdx === idx ? -1 : idx
      this.password = ''
    },
    async connectAp() {
      const ap = this.aps[this.selectedIdx]
      if (!ap) return
      const pwd = ap.authmode === 0 ? '' : this.password

      try {
        const res = await fetch('/api/wifi/connect', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ ssid: ap.ssid, password: pwd })
        })
        const data = await res.json()

        if (data.status === 'ok') {
          this.newSsid = ap.ssid
          this.switchState = 'switching'
          this.startPolling()
        } else {
          alert(data.error || '连接请求失败')
        }
      } catch (e) {
        alert('请求失败：' + e.message)
      }
    },
    startPolling() {
      this.pollTimer = setInterval(() => this.pollStatus(), 1000)
    },
    async pollStatus() {
      try {
        const res = await fetch('/api/wifi/status')
        const data = await res.json()
        this.switchState = data.state
        this.currentSsid = data.current_ssid
        this.newSsid = data.new_ssid
        this.rssi = data.rssi
        this.elapsedSec = data.elapsed_sec

        if (data.state !== 'switching') {
          clearInterval(this.pollTimer)
          this.pollTimer = null

          // 成功或回退后更新 currentSsid
          if (data.state === 'connected' || data.state === 'failed') {
            this.currentSsid = data.current_ssid
            // 自动刷新 AP 列表
            setTimeout(() => this.startScan(), 2000)
          }
        }
      } catch (e) {
        // 切换过程中设备可能短暂断网，忽略错误
      }
    },
    dismissFail() {
      this.switchState = 'idle'
      this.newSsid = ''
    }
  },
  async mounted() {
    // 启动时只获取当前状态，不自动扫描
    try {
      const res = await fetch('/api/wifi/status')
      const data = await res.json()
      this.switchState = data.state
      this.currentSsid = data.current_ssid
      this.rssi = data.rssi

      if (data.state === 'switching') {
        this.newSsid = data.new_ssid
        this.elapsedSec = data.elapsed_sec
        this.startPolling()
      }
    } catch (e) {
      this.scanError = '无法连接设备'
    }
  },
  beforeUnmount() {
    if (this.pollTimer) clearInterval(this.pollTimer)
  }
}
</script>

<style scoped>
.wifi-page {
  position: relative;
  padding-bottom: 16px;
  background: linear-gradient(160deg, #e8f0fe 0%, #d4e4fc 30%, #ffffff 100%);
}

/* ── 动态粒子背景 ── */
.bg-particles {
  position: absolute;
  inset: 0;
  pointer-events: none;
  overflow: hidden;
}
.particle {
  position: absolute;
  border-radius: 50%;
  background: rgba(24, 119, 242, 0.15);
  animation: floatUp 6s ease-in-out infinite;
}
@keyframes floatUp {
  0%, 100% { transform: translateY(0) scale(1); opacity: 0.3; }
  50% { transform: translateY(-40px) scale(1.8); opacity: 0.8; }
}

/* ── 状态头部 ── */
.status-header {
  position: relative;
  margin: 16px 16px 0;
  padding: 24px 20px;
  border-radius: 16px;
  background: linear-gradient(135deg, #1565c0, #1976d2);
  color: #fff;
  display: flex;
  align-items: center;
  gap: 16px;
  box-shadow: 0 4px 20px rgba(21, 101, 192, 0.3);
  transition: background 0.6s;
}
.status-header.header-switching {
  background: linear-gradient(135deg, #e65100, #f57c00);
}
.status-header.header-failed {
  background: linear-gradient(135deg, #c62828, #d32f2f);
}
.status-header.header-connected {
  background: linear-gradient(135deg, #2e7d32, #388e3c);
}

.wifi-icon-wrap {
  flex-shrink: 0;
}
.wifi-icon {
  width: 40px;
  height: 40px;
  fill: rgba(255,255,255,0.9);
}
.wifi-icon.scanning {
  animation: wifiPulse 0.6s ease-in-out infinite alternate;
}
.wifi-icon.connected {
  fill: #fff;
}
@keyframes wifiPulse {
  from { opacity: 0.5; transform: scale(0.95); }
  to   { opacity: 1;   transform: scale(1.05); }
}

.status-text {
  flex: 1;
  min-width: 0;
}
.status-text h2 {
  font-size: 18px;
  font-weight: 600;
  margin: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.status-text p {
  font-size: 13px;
  opacity: 0.85;
  margin: 4px 0 0;
}

/* 倒计时圆环 */
.countdown-ring {
  flex-shrink: 0;
  width: 56px;
  height: 56px;
  position: relative;
}
.countdown-ring svg {
  width: 100%;
  height: 100%;
  transform: rotate(-90deg);
}
.countdown-bg {
  fill: none;
  stroke: rgba(255,255,255,0.2);
  stroke-width: 3;
}
.countdown-fg {
  fill: none;
  stroke: #fff;
  stroke-width: 3;
  stroke-linecap: round;
  stroke-dasharray: 163.36; /* 2*π*26 */
  transition: stroke-dashoffset 1s linear;
}
.countdown-text {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  font-weight: 700;
}

/* ── 进度条 ── */
.progress-section {
  margin: 12px 16px 0;
}
.progress-bar {
  height: 4px;
  background: rgba(21, 101, 192, 0.12);
  border-radius: 2px;
  overflow: hidden;
}
.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #1976d2, #42a5f5);
  border-radius: 2px;
  transition: width 0.5s ease;
}
.progress-label {
  font-size: 12px;
  color: #1565c0;
  margin: 6px 0 0;
  text-align: center;
}

/* ── 操作按钮 ── */
.action-bar {
  margin: 16px;
}
.btn-scan {
  width: 100%;
  padding: 14px 24px;
  border: none;
  border-radius: 12px;
  background: #1976d2;
  color: #fff;
  font-size: 15px;
  font-weight: 500;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  box-shadow: 0 2px 12px rgba(25, 118, 210, 0.3);
  transition: background 0.2s, transform 0.15s;
}
.btn-scan:hover:not(:disabled) {
  background: #1565c0;
  transform: translateY(-1px);
}
.btn-scan:disabled {
  background: #90caf9;
  cursor: not-allowed;
  box-shadow: none;
}
.btn-icon {
  display: inline-block;
  font-size: 20px;
  line-height: 1;
}
.btn-icon.spinning {
  animation: spin 0.8s linear infinite;
}
@keyframes spin {
  from { transform: rotate(0deg); }
  to   { transform: rotate(360deg); }
}

/* ── AP 列表 ── */
.ap-list {
  margin: 0 16px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.ap-item {
  background: rgba(255,255,255,0.85);
  backdrop-filter: blur(8px);
  border-radius: 12px;
  overflow: hidden;
  border: 1.5px solid transparent;
  transition: border-color 0.2s, box-shadow 0.2s;
  cursor: pointer;
}
.ap-item:hover {
  border-color: #bbdefb;
  box-shadow: 0 2px 8px rgba(25,118,210,0.08);
}
.ap-item.selected {
  border-color: #1976d2;
  box-shadow: 0 4px 16px rgba(25,118,210,0.12);
}

.ap-main {
  display: flex;
  align-items: center;
  padding: 14px 16px;
  gap: 12px;
}

/* 信号强度条 */
.ap-signal {
  flex-shrink: 0;
}
.signal-bars {
  display: flex;
  align-items: flex-end;
  gap: 2px;
  height: 20px;
}
.signal-bars i {
  display: block;
  width: 4px;
  border-radius: 1px;
  background: #e0e0e0;
  transition: background 0.3s;
}
.signal-bars i:nth-child(1) { height: 5px; }
.signal-bars i:nth-child(2) { height: 9px; }
.signal-bars i:nth-child(3) { height: 14px; }
.signal-bars i:nth-child(4) { height: 20px; }

.signal-bars.sig-4 i { background: #2e7d32; }
.signal-bars.sig-3 i { background: #43a047; }
.signal-bars.sig-3 i:nth-child(4) { background: #e0e0e0; }
.signal-bars.sig-2 i { background: #f9a825; }
.signal-bars.sig-2 i:nth-child(3),
.signal-bars.sig-2 i:nth-child(4) { background: #e0e0e0; }
.signal-bars.sig-1 i { background: #e65100; }
.signal-bars.sig-1 i:nth-child(2),
.signal-bars.sig-1 i:nth-child(3),
.signal-bars.sig-1 i:nth-child(4) { background: #e0e0e0; }
.signal-bars.sig-0 i { background: #bdbdbd; }
.signal-bars.sig-0 i:nth-child(1),
.signal-bars.sig-0 i:nth-child(2),
.signal-bars.sig-0 i:nth-child(3) { background: #e0e0e0; }

.ap-info {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.ap-ssid {
  font-size: 15px;
  font-weight: 500;
  color: #212121;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.ap-meta {
  font-size: 12px;
  color: #757575;
  display: flex;
  align-items: center;
  gap: 6px;
}
.lock-icon { font-size: 11px; }
.lock-icon.open { opacity: 0.4; }

.ap-arrow {
  font-size: 22px;
  color: #bdbdbd;
  transition: transform 0.25s;
}
.ap-item.selected .ap-arrow {
  transform: rotate(90deg);
  color: #1976d2;
}

/* 密码输入区 */
.ap-password {
  padding: 0 16px 14px;
  display: flex;
  gap: 8px;
  animation: slideDown 0.2s ease;
}
@keyframes slideDown {
  from { opacity: 0; transform: translateY(-6px); }
  to   { opacity: 1; transform: translateY(0); }
}
.pwd-input {
  flex: 1;
  padding: 10px 14px;
  border: 1.5px solid #e0e0e0;
  border-radius: 8px;
  font-size: 14px;
  outline: none;
  transition: border-color 0.2s;
}
.pwd-input:focus {
  border-color: #1976d2;
}
.pwd-input:disabled {
  background: #f5f5f5;
  color: #9e9e9e;
}
.btn-connect {
  padding: 10px 20px;
  border: none;
  border-radius: 8px;
  background: #1976d2;
  color: #fff;
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  white-space: nowrap;
  transition: background 0.2s;
}
.btn-connect:hover:not(:disabled) {
  background: #1565c0;
}
.btn-connect:disabled {
  background: #90caf9;
  cursor: not-allowed;
}
.btn-connect.pulsing {
  animation: btnPulse 1s ease-in-out infinite;
}
@keyframes btnPulse {
  0%, 100% { box-shadow: 0 0 0 0 rgba(25,118,210,0.4); }
  50%      { box-shadow: 0 0 0 8px rgba(25,118,210,0); }
}

/* ── 空态 / 错误 ── */
.empty-state, .error-state {
  margin: 40px 16px;
  text-align: center;
  color: #9e9e9e;
}
.empty-icon {
  font-size: 48px;
  margin-bottom: 12px;
}
.empty-state p, .error-state p {
  font-size: 14px;
}

/* ── 结果通知 ── */
.fail-banner, .success-banner {
  margin: 16px;
  padding: 14px 16px;
  border-radius: 10px;
  font-size: 14px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  animation: slideDown 0.3s ease;
}
.fail-banner {
  background: #ffebee;
  color: #c62828;
}
.success-banner {
  background: #e8f5e9;
  color: #2e7d32;
}
.btn-dismiss {
  flex-shrink: 0;
  padding: 6px 14px;
  border: 1px solid #ef9a9a;
  border-radius: 6px;
  background: #fff;
  color: #c62828;
  font-size: 13px;
  cursor: pointer;
}
</style>
