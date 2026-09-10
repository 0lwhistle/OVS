<template>
  <span class="status-dot" :class="status"></span>
</template>

<script setup>
defineProps({
  status: { type: String, default: 'ok' }, // ok | warn | off
})
</script>

<style scoped>
.status-dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

.status-dot.ok {
  background: var(--ovs-success);
  box-shadow: 0 0 0 3px var(--ovs-success-soft);
}

.status-dot.warn {
  background: var(--ovs-warning);
  box-shadow: 0 0 0 3px var(--ovs-warning-soft);
}

.status-dot.off {
  background: var(--ovs-text-3);
  box-shadow: 0 0 0 3px #F0F2F6;
}
</style>
