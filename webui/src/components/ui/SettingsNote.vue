<template>
  <!-- `info`: quiet footnote under a page. `warning`: tinted notice kept inline,
       next to the controls it is about, never hidden in a dialog. -->
  <div class="note" :class="`note-${tone}`" :role="tone === 'warning' ? 'note' : undefined">
    <component
      :is="tone === 'warning' ? WarningIcon : InformationOutlineIcon"
      class="shrink-0"
      :size="20"
      aria-hidden="true"
    />
    <p class="text-xs leading-relaxed"><slot /></p>
  </div>
</template>

<script setup>
import WarningIcon from '@/components/icons/Warning.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'

defineProps({
  tone: { type: String, default: 'info' }, // 'info' | 'warning'
})
</script>

<style scoped>
.note {
  display: flex;
  gap: 12px;
  align-items: flex-start;
}

.note-info {
  padding: 0 4px;
  margin-bottom: 32px;
  color: var(--color-on-surface-variant);
}

.note-warning {
  padding: 14px 16px;
  margin-bottom: 16px;
  border-radius: 20px;
  background: var(--color-tertiary-container);
  color: var(--color-on-tertiary-container);
}
</style>
