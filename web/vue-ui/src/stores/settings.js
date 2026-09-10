/**
 * 轻量设置 store（自研，无 pinia）：语言偏好 + 持久化。
 * 语言包本体由 i18n 模块管理，本 store 只负责用户选择与恢复。
 */
import { ref } from 'vue'
import { setLang, loadLanguage, FALLBACK_LANG } from '../i18n'

const LANG_KEY = 'ovs.web.lang'

function guessInitial() {
  const saved = localStorage.getItem(LANG_KEY)
  if (saved === 'zh-CN' || saved === 'en-US') return saved
  return navigator.language?.startsWith('zh') ? 'zh-CN' : FALLBACK_LANG
}

const lang = ref(guessInitial())
setLang(lang.value) // 内置包同步生效，避免首帧英文闪烁

async function init() {
  await loadLanguage(lang.value)
}

async function setLanguage(code) {
  lang.value = code
  localStorage.setItem(LANG_KEY, code)
  await loadLanguage(code)
}

export function useSettings() {
  return { lang, setLanguage, init }
}
