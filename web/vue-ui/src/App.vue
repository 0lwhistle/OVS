<template>
  <div class="app-shell">
    <!-- 主内容区 -->
    <main class="page-content">
      <DashboardView v-if="route.path === '/'" />
      <NetworkView v-else-if="route.path === '/network'" />
      <FirmwareView v-else-if="route.path === '/firmware'" />
      <SettingsView v-else />
    </main>

    <!-- 底部导航 -->
    <nav class="bottom-nav">
      <button v-for="r in routes" :key="r.path" class="nav-btn"
              :class="{ active: path === r.path }"
              @click="navigate(r.path)">
        <AppIcon :name="r.icon" :size="22" />
        <span>{{ t(r.navKey) }}</span>
      </button>
    </nav>
  </div>
</template>

<script setup>
import { watchEffect } from 'vue'
import { useRouter, routes } from './router'
import { t } from './i18n'
import DashboardView from './views/DashboardView.vue'
import NetworkView from './views/NetworkView.vue'
import FirmwareView from './views/FirmwareView.vue'
import SettingsView from './views/SettingsView.vue'
import AppIcon from './components/AppIcon.vue'

const { path, route, navigate } = useRouter()

/* 标题随语言即时切换（index.html 静态标题由运行时覆盖） */
watchEffect(() => {
  document.title = t('APP_TITLE')
})
</script>

<style scoped>
.app-shell {
  display: flex;
  flex-direction: column;
  height: 100vh;
  max-width: 480px;
  margin: 0 auto;
  overflow: hidden;
  background: var(--ovs-page-bg);
}

.page-content {
  flex: 1;
  overflow-y: auto;
  min-height: 0;
}

/* 底部导航 —— 白底 + 主蓝高亮 */
.bottom-nav {
  display: flex;
  background: var(--ovs-card-bg);
  border-top: 1px solid var(--ovs-divider);
  padding: 6px 0 max(6px, env(safe-area-inset-bottom));
  flex-shrink: 0;
}

.nav-btn {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  padding: 6px 0;
  border: none;
  background: none;
  color: var(--ovs-text-3);
  font-size: 11px;
  cursor: pointer;
  transition: color 0.2s;
}

.nav-btn.active {
  color: var(--ovs-primary);
}
</style>
