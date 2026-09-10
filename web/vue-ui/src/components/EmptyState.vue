<template>
  <div class="empty-state">
    <AppIcon :name="icon" :size="34" />
    <p>{{ text }}</p>
  </div>
</template>

<script setup>
import AppIcon from './AppIcon.vue'

defineProps({
  text: { type: String, required: true },
  icon: { type: String, default: 'alert' },
})
</script>

<style scoped>
.empty-state {
  text-align: center;
  padding: 26px 12px;
  color: var(--ovs-text-3);
}

.empty-state p {
  margin-top: 8px;
  font-size: 13px;
}
</style>
