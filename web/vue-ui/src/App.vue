<template>
  <div class="app-shell">
    <!-- 主内容区 -->
    <main class="page-content">
      <Dashboard v-if="page === 'home'" />
      <WifiConfig v-else-if="page === 'wifi'" />
      <OtaUpdate v-else-if="page === 'ota'" />
    </main>

    <!-- 底部导航 -->
    <nav class="bottom-nav">
      <button class="nav-btn" :class="{ active: page === 'home' }" @click="page = 'home'">
        <svg viewBox="0 0 24 24" class="nav-icon">
          <path d="M3 13h8V3H3v10zm0 8h8v-6H3v6zm10 0h8V11h-8v10zm0-18v6h8V3h-8z"/>
        </svg>
        <span>仪表盘</span>
      </button>
      <button class="nav-btn" :class="{ active: page === 'wifi' }" @click="page = 'wifi'">
        <svg viewBox="0 0 24 24" class="nav-icon">
          <path d="M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C17.93 4.04 6.07 4.04 1 9zm8 8l3 3 3-3c-1.65-1.66-4.34-1.66-6 0zm-6-6l2 2c2.76-2.76 7.24-2.76 10 0l2-2C13.14 7.14 5.86 7.14 3 11z"/>
        </svg>
        <span>Wi-Fi</span>
      </button>
      <button class="nav-btn" :class="{ active: page === 'ota' }" @click="page = 'ota'">
        <svg viewBox="0 0 24 24" class="nav-icon">
          <path d="M19.35 10.04C18.67 6.59 15.64 4 12 4 9.11 4 6.6 5.64 5.35 8.04 2.34 8.36 0 10.91 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96zM14 13v4h-4v-4H7l5-5 5 5h-3z"/>
        </svg>
        <span>OTA</span>
      </button>
    </nav>
  </div>
</template>

<script>
import Dashboard from './components/Dashboard.vue'
import WifiConfig from './components/WifiConfig.vue'
import OtaUpdate from './components/OtaUpdate.vue'

export default {
  components: { Dashboard, WifiConfig, OtaUpdate },
  data() {
    return {
      page: 'home'
    }
  }
}
</script>

<style>
/* 全局重置 */
*, *::before, *::after {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', sans-serif;
  background: #f5f7fa;
  color: #212121;
  -webkit-font-smoothing: antialiased;
}

.app-shell {
  display: flex;
  flex-direction: column;
  height: 100vh;
  max-width: 480px;
  margin: 0 auto;
  overflow: hidden;
}

.page-content {
  flex: 1;
  overflow-y: auto;
  min-height: 0;
}

/* 底部导航 — 始终固定在底部 */
.bottom-nav {
  display: flex;
  background: #fff;
  border-top: 1px solid #e8eaed;
  padding: 6px 0;
  padding-bottom: max(6px, env(safe-area-inset-bottom));
  flex-shrink: 0;
}

.nav-btn {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 6px 0;
  border: none;
  background: none;
  color: #9e9e9e;
  font-size: 11px;
  cursor: pointer;
  transition: color 0.2s;
}

.nav-btn.active {
  color: #1976d2;
}

.nav-icon {
  width: 24px;
  height: 24px;
  fill: currentColor;
}
</style>
