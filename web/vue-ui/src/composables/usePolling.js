/**
 * 轮询 composable：定时刷新 + 手动刷新 + 页面隐藏时暂停 + 卸载自清理。
 */
import { ref, onUnmounted } from 'vue'

/**
 * @param {() => Promise<any>} fn 拉取函数（异常由 error 捕获，不中断轮询）
 * @param {object} [opts]
 * @param {number} [opts.intervalMs=30000] 轮询间隔
 * @param {boolean} [opts.immediate=true] 立即执行首次拉取
 */
export function usePolling(fn, { intervalMs = 30000, immediate = true } = {}) {
  const loading = ref(false)
  const error = ref(null)
  const lastUpdated = ref(0)

  let timer = null
  let busy = false
  let running = false

  async function refresh() {
    if (busy) return
    busy = true
    loading.value = true
    try {
      await fn()
      error.value = null
      lastUpdated.value = Date.now()
    } catch (e) {
      error.value = e
    } finally {
      busy = false
      loading.value = false
    }
  }

  function start() {
    if (running) return
    running = true
    if (immediate) refresh()
    timer = setInterval(() => {
      if (!document.hidden) refresh()
    }, intervalMs)
  }

  function stop() {
    running = false
    if (timer) { clearInterval(timer); timer = null }
  }

  const onVisibility = () => {
    if (document.hidden) stop()
    else start()
  }
  document.addEventListener('visibilitychange', onVisibility)
  start()

  onUnmounted(() => {
    stop()
    document.removeEventListener('visibilitychange', onVisibility)
  })

  return { loading, error, lastUpdated, refresh, start, stop }
}
