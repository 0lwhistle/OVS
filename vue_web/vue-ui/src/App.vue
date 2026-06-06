<template>
  <div class="app">
    <h1>🔧 ESP32 控制面板</h1>
    
    <!-- LED 控制 -->
    <div class="card">
      <h2>LED 控制</h2>
      <p>状态: <span :class="ledStatus ? 'on' : 'off'">
        {{ ledStatus ? '🟢 已开启' : '🔴 已关闭' }}
      </span></p>
      <button @click="toggleLed" :disabled="loading">
        {{ loading ? '处理中...' : '切换 LED' }}
      </button>
    </div>

    <!-- 系统信息 -->
    <div class="card">
      <h2>系统信息</h2>
      <p>运行时间: {{ uptime }} 秒</p>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'

const ledStatus = ref(false)
const uptime = ref(0)
const loading = ref(false)

// 获取状态
async function fetchStatus() {
  try {
    const res = await fetch('/api/status')
    const data = await res.json()
    ledStatus.value = data.led === 1
    uptime.value = Math.floor(data.uptime / 1000)
  } catch (e) {
    console.error('获取状态失败:', e)
  }
}

// 切换 LED
async function toggleLed() {
  loading.value = true
  try {
    const res = await fetch('/api/toggle')
    const data = await res.json()
    ledStatus.value = data.led === 1
  } catch (e) {
    console.error('切换失败:', e)
  }
  loading.value = false
}

// 页面加载时获取状态
onMounted(() => {
  fetchStatus()
  // 每 5 秒刷新一次
  setInterval(fetchStatus, 5000)
})
</script>

<style>
.app {
  font-family: Arial, sans-serif;
  max-width: 600px;
  margin: 0 auto;
  padding: 20px;
}
.card {
  background: #f5f5f5;
  border-radius: 8px;
  padding: 20px;
  margin: 20px 0;
}
.on { color: green; font-weight: bold; }
.off { color: red; }
button {
  padding: 10px 24px;
  font-size: 16px;
  cursor: pointer;
  background: #4CAF50;
  color: white;
  border: none;
  border-radius: 4px;
}
button:disabled {
  background: #ccc;
  cursor: not-allowed;
}
</style>
