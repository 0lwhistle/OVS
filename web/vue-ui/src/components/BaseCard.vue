<template>
  <section class="base-card">
    <header v-if="title" class="card-head">
      <span v-if="icon" class="card-icon"><AppIcon :name="icon" :size="18" /></span>
      <h3 class="card-title">{{ title }}</h3>
      <div class="card-extra"><slot name="extra" /></div>
    </header>
    <slot />
  </section>
</template>

<script setup>
import AppIcon from './AppIcon.vue'

defineProps({
  title: { type: String, default: '' },
  icon: { type: String, default: '' },
})
</script>

<style scoped>
.base-card {
  background: var(--ovs-card-bg);
  border-radius: var(--ovs-radius);
  box-shadow: var(--ovs-shadow);
  padding: 18px;
  margin-bottom: 14px;
}

.card-head {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 14px;
}

.card-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 30px;
  height: 30px;
  border-radius: 9px;
  background: var(--ovs-primary-soft);
  color: var(--ovs-primary);
  flex-shrink: 0;
}

.card-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--ovs-text-1);
}

.card-extra {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 8px;
}
</style>
