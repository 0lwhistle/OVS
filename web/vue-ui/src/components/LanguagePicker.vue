<template>
  <div class="lang-picker" role="radiogroup">
    <button
      v-for="l in langs"
      :key="l.code"
      class="lang-opt"
      :class="{ active: lang === l.code }"
      role="radio"
      :aria-checked="lang === l.code"
      @click="setLanguage(l.code)"
    >{{ l.label }}</button>
  </div>
</template>

<script setup>
import { useI18n } from '../i18n'
import { useSettings } from '../stores/settings'

const { langs } = useI18n()
const { lang, setLanguage } = useSettings()
</script>

<style scoped>
.lang-picker {
  display: flex;
  background: #F0F3F8;
  border-radius: var(--ovs-radius-sm);
  padding: 3px;
  gap: 3px;
}

.lang-opt {
  flex: 1;
  padding: 8px 14px;
  border: none;
  border-radius: 8px;
  background: transparent;
  font-size: 13px;
  font-weight: 500;
  color: var(--ovs-text-2);
  cursor: pointer;
  transition: all 0.18s;
  white-space: nowrap;
}

.lang-opt.active {
  background: #fff;
  color: var(--ovs-primary);
  font-weight: 600;
  box-shadow: 0 1px 4px rgba(26, 42, 80, 0.12);
}
</style>
