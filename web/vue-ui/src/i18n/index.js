/**
 * i18n：与板端 LVGL 共用同一翻译源（设备端 /i18n/<lang>.json）。
 * 加载顺序：fetch 设备语言包（单一事实来源）→ 内置包（打包进固件，
 * 设备无语言包/开发环境兜底）→ en-US 回退 → 键名原文。
 * schema 契约见 docs/three_tasks_plan.md §3 I1：
 *   {"lang":"zh-CN","ver":1,"strings":{"HOME_TEMP":"温度",...}}
 */
import { reactive, shallowRef, computed } from 'vue'
import zhCN from './locales/zh-CN.json'
import enUS from './locales/en-US.json'

export const FALLBACK_LANG = 'en-US'
export const LANGS = [
  { code: 'zh-CN', label: '简体中文' },
  { code: 'en-US', label: 'English' },
]

const BUNDLED = { 'zh-CN': zhCN, 'en-US': enUS }

const state = reactive({ lang: FALLBACK_LANG })
const packs = shallowRef({ ...BUNDLED })

function stringsOf(lang) {
  const pack = packs.value[lang]
  return pack && pack.strings ? pack.strings : null
}

/**
 * 取文案：当前语言 → 英文 → 键名原文（开发期控制台告警）。
 * @param {string} key i18n 键（契约：大写下划线）
 * @param {Record<string, string|number>} [params] {n} 形式插值
 */
export function t(key, params) {
  let text = stringsOf(state.lang)?.[key]
  if (text == null) text = stringsOf(FALLBACK_LANG)?.[key]
  if (text == null) {
    if (import.meta.env.DEV) console.warn(`[i18n] missing key: ${key}`)
    return key
  }
  if (params) {
    for (const name in params) {
      text = text.replaceAll(`{${name}}`, String(params[name]))
    }
  }
  return text
}

/** 同步切换语言（内置包立即可用） */
export function setLang(lang) {
  state.lang = BUNDLED[lang] ? lang : FALLBACK_LANG
}

/**
 * 加载语言：优先设备端 /i18n/<lang>.json（与 LVGL 同源），
 * 失败（开发环境/未部署）静默回退内置包。
 */
export async function loadLanguage(lang) {
  const target = BUNDLED[lang] ? lang : FALLBACK_LANG
  try {
    const res = await fetch(`/i18n/${target}.json`, { cache: 'no-store' })
    if (res.ok) {
      const pack = await res.json()
      if (pack && pack.lang === target && pack.strings &&
          typeof pack.strings === 'object') {
        packs.value = { ...packs.value, [target]: pack }
      }
    }
  } catch (e) {
    /* 设备端语言包不可用 → 内置包兜底 */
  }
  setLang(target)
  return target
}

export function useI18n() {
  return {
    t,
    lang: computed(() => state.lang),
    langs: LANGS,
    setLanguage: loadLanguage,
  }
}
