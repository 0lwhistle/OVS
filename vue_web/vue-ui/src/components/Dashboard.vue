<template>
  <div class="dashboard">
    <header class="dash-header">
      <h1>ESP32-S3 控制面板</h1>
      <p class="status" :class="{ online: connected, offline: !connected }">
        {{ connected ? '🟢 已连接' : '🔴 未连接' }}
      </p>
    </header>

    <main class="dash-main">
      <section class="card">
        <h2>设备信息</h2>
        <div class="info-grid">
          <div class="info-item">
            <span class="label">设备</span>
            <span class="value">{{ deviceInfo.device }}</span>
          </div>
          <div class="info-item">
            <span class="label">运行时间</span>
            <span class="value">{{ formatUptime(deviceInfo.uptime_ms) }}</span>
          </div>
        </div>
      </section>

      <section class="card">
        <h2>API 测试</h2>
        <div class="btn-group">
          <button @click="fetchHello" :disabled="loading">
            {{ loading ? '请求中...' : 'GET /api/hello' }}
          </button>
          <button @click="fetchStatus" :disabled="loading">
            {{ loading ? '请求中...' : 'GET /api/status' }}
          </button>
        </div>
        <pre v-if="apiResult" class="result">{{ apiResult }}</pre>
      </section>
    </main>

    <footer class="dash-footer">
      <p>ESP32-S3 + Mongoose + Vue.js</p>
    </footer>
  </div>
</template>

<script>
export default {
  data() {
    return {
      connected: false,
      loading: false,
      apiResult: null,
      deviceInfo: {
        device: 'ESP32-S3',
        uptime_ms: 0
      }
    }
  },
  methods: {
    async fetchHello() {
      this.loading = true
      try {
        const res = await fetch('/api/hello')
        const data = await res.json()
        this.apiResult = JSON.stringify(data, null, 2)
        this.connected = true
      } catch (e) {
        this.apiResult = 'Error: ' + e.message
        this.connected = false
      }
      this.loading = false
    },
    async fetchStatus() {
      this.loading = true
      try {
        const res = await fetch('/api/status')
        const data = await res.json()
        this.deviceInfo = data
        this.apiResult = JSON.stringify(data, null, 2)
        this.connected = true
      } catch (e) {
        this.apiResult = 'Error: ' + e.message
        this.connected = false
      }
      this.loading = false
    },
    formatUptime(ms) {
      if (!ms) return '未知'
      const seconds = Math.floor(ms / 1000)
      const minutes = Math.floor(seconds / 60)
      const hours = Math.floor(minutes / 60)
      return `${hours}时 ${minutes % 60}分 ${seconds % 60}秒`
    }
  },
  mounted() {
    this.fetchStatus()
  }
}
</script>

<style scoped>
.dashboard {
  padding: 16px;
}

.dash-header {
  text-align: center;
  margin-bottom: 24px;
}

.dash-header h1 {
  font-size: 20px;
  margin-bottom: 8px;
  color: #212121;
}

.status {
  font-size: 13px;
  padding: 4px 12px;
  border-radius: 12px;
  display: inline-block;
}
.status.online { background: #e8f5e9; color: #2e7d32; }
.status.offline { background: #fbe9e7; color: #c62828; }

.card {
  background: white;
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 16px;
  box-shadow: 0 1px 4px rgba(0,0,0,0.06);
}

.card h2 {
  font-size: 15px;
  margin-bottom: 14px;
  color: #666;
}

.info-grid { display: grid; gap: 10px; }
.info-item { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #f0f0f0; }
.info-item .label { color: #999; }
.info-item .value { font-weight: 500; }

.btn-group { display: flex; gap: 8px; margin-bottom: 10px; }
button {
  flex: 1;
  padding: 10px 14px;
  border: none;
  border-radius: 8px;
  background: #1976d2;
  color: white;
  font-size: 13px;
  cursor: pointer;
  transition: background 0.2s;
}
button:hover:not(:disabled) { background: #1565c0; }
button:disabled { background: #90caf9; cursor: not-allowed; }

.result {
  background: #f5f5f5;
  padding: 12px;
  border-radius: 8px;
  font-size: 12px;
  overflow-x: auto;
  white-space: pre-wrap;
  word-break: break-all;
}

.dash-footer {
  text-align: center;
  color: #999;
  font-size: 12px;
  margin-top: 24px;
}
</style>
