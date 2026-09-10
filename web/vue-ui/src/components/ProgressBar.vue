<template>
  <div class="progress-wrap">
    <div class="progress-track">
      <div class="progress-fill" :class="tone" :style="{ width: clamped + '%' }"></div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  value: { type: Number, default: 0 },
  tone: { type: String, default: 'primary' }, // primary | success | danger
})

const clamped = computed(() => Math.max(0, Math.min(100, props.value)))
</script>

<style scoped>
.progress-track {
  height: 8px;
  background: var(--ovs-divider);
  border-radius: 4px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  border-radius: 4px;
  transition: width 0.35s ease;
}

.progress-fill.primary {
  background: var(--ovs-primary-gradient);
}

.progress-fill.success {
  background: linear-gradient(90deg, #16A34A, #4ADE80);
}

.progress-fill.danger {
  background: linear-gradient(90deg, #DC2626, #F87171);
}
</style>
