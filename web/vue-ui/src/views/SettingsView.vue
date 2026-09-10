<template>
  <div class="page">
    <div class="page-head">
      <h2 class="page-title">{{ t('SET_TITLE') }}</h2>
    </div>

    <!-- 语言切换（即时生效） -->
    <BaseCard :title="t('SET_LANGUAGE')" icon="lang">
      <div class="set-row">
        <span class="set-label">{{ t('WEB_LANG') }}</span>
        <LanguagePicker />
      </div>
      <p class="hint">{{ t('WEB_LANG_DESC') }}</p>
    </BaseCard>

    <!-- 占位项（后续接入板端能力） -->
    <BaseCard :title="t('SET_VOLUME')" icon="thermo">
      <div class="set-row placeholder">
        <span class="set-label">{{ t('SET_VOLUME') }}</span>
        <span class="placeholder-chip">{{ t('WEB_SET_PLACEHOLDER') }}</span>
      </div>
    </BaseCard>

    <BaseCard :title="t('SET_DEVICE_NAME')" icon="chip">
      <div class="set-row placeholder">
        <span class="set-label">{{ t('SET_DEVICE_NAME') }}</span>
        <span class="placeholder-chip">{{ t('WEB_SET_PLACEHOLDER') }}</span>
      </div>
    </BaseCard>

    <BaseCard :title="t('SET_STANDBY')" icon="clock">
      <div class="set-row placeholder">
        <span class="set-label">{{ t('SET_STANDBY') }}</span>
        <span class="placeholder-chip">{{ t('WEB_SET_PLACEHOLDER') }}</span>
      </div>
    </BaseCard>
  </div>
</template>

<script setup>
import { t } from '../i18n'
import BaseCard from '../components/BaseCard.vue'
import LanguagePicker from '../components/LanguagePicker.vue'
</script>

<style scoped>
.page {
  padding: 16px;
}

.page-head {
  margin-bottom: 14px;
}

.page-title {
  font-size: 20px;
  font-weight: 700;
  color: var(--ovs-text-1);
}

.set-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.set-row.placeholder {
  opacity: 0.55;
}

.set-label {
  font-size: 14px;
  font-weight: 500;
  color: var(--ovs-text-1);
}

.placeholder-chip {
  font-size: 11.5px;
  padding: 3px 10px;
  border-radius: 999px;
  background: #F0F3F8;
  color: var(--ovs-text-3);
}

.hint {
  margin-top: 12px;
  font-size: 12px;
  color: var(--ovs-text-3);
  line-height: 1.6;
}
</style>
