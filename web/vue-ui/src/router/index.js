/**
 * 自研轻量 hash 路由（不引 vue-router，控制固件 SPIFFS 体积）。
 * 路由表驱动，支持浏览器前进/后退（hashchange）。
 */
import { ref, computed } from 'vue'
import DashboardView from '../views/DashboardView.vue'
import NetworkView from '../views/NetworkView.vue'
import FirmwareView from '../views/FirmwareView.vue'
import SettingsView from '../views/SettingsView.vue'

export const routes = [
  { path: '/', component: DashboardView, titleKey: 'WEB_DASH_TITLE', navKey: 'WEB_NAV_DASH', icon: 'dash' },
  { path: '/network', component: NetworkView, titleKey: 'NET_TITLE', navKey: 'WEB_NAV_NETWORK', icon: 'wifi' },
  { path: '/firmware', component: FirmwareView, titleKey: 'WEB_FW_TITLE', navKey: 'WEB_NAV_FIRMWARE', icon: 'chip' },
  { path: '/settings', component: SettingsView, titleKey: 'SET_TITLE', navKey: 'WEB_NAV_SETTINGS', icon: 'gear' },
]

function normalize(hash) {
  const path = (hash || '').replace(/^#/, '')
  return routes.some((r) => r.path === path) ? path : '/'
}

const current = ref(normalize(window.location.hash))

window.addEventListener('hashchange', () => {
  current.value = normalize(window.location.hash)
})

export function useRouter() {
  return {
    path: computed(() => current.value),
    route: computed(() => routes.find((r) => r.path === current.value) || routes[0]),
    navigate(path) { window.location.hash = path },
  }
}
