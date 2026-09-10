import { createApp } from 'vue'
import App from './App.vue'
import './style.css'
import { useSettings } from './stores/settings'

const app = createApp(App)

/* 恢复语言偏好（内置包同步可用，设备语言包后台增强加载） */
useSettings().init()

app.mount('#app')
