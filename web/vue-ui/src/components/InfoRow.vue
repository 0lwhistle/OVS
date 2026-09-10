<template>
  <div class="info-row">
    <span class="row-label">{{ label }}</span>
    <span class="row-value" :class="{ mono, dim }">{{ value }}</span>
  </div>
</template>

<script setup>
defineProps({
  label: { type: String, required: true },
  value: { type: [String, Number], default: '' },
  mono: { type: Boolean, default: false },
  dim: { type: Boolean, default: false },
})
</script>

<style scoped>
.info-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 8px 0;
}

.info-row + .info-row {
  border-top: 1px solid var(--ovs-divider);
}

.row-label {
  font-size: 13px;
  color: var(--ovs-text-2);
  flex-shrink: 0;
}

.row-value {
  font-size: 13px;
  color: var(--ovs-text-1);
  font-weight: 500;
  text-align: right;
  word-break: break-all;
}

.row-value.mono {
  font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', monospace;
  font-size: 12.5px;
}

.row-value.dim {
  color: var(--ovs-text-3);
}
</style>
