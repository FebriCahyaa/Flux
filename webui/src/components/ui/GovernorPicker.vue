<template>
  <!-- One profile's governor: a card with every available governor as a chip. -->
  <section class="picker m3-card">
    <div class="flex items-start gap-4">
      <span class="badge" :class="[shape, tone]"><component :is="icon" :size="22" /></span>
      <div class="flex-1 min-w-0">
        <h3 class="text-base font-semibold text-on-surface">{{ title }}</h3>
        <p class="text-xs text-on-surface-variant mt-1 leading-relaxed">{{ description }}</p>
      </div>
    </div>

    <div v-if="options.length" class="chips mt-4" role="radiogroup" :aria-label="title">
      <button
        v-for="o in options"
        :key="o.value"
        role="radio"
        :aria-checked="o.value === modelValue"
        class="chip m3-press"
        :class="{ on: o.value === modelValue, risky: riskyValues.includes(o.value) }"
        @click="o.value !== modelValue && $emit('select', o.value)"
      >
        <svg v-if="o.value === modelValue" class="check" viewBox="0 0 24 24" aria-hidden="true">
          <path d="M9.55 18L3.85 12.3L5.275 10.875L9.55 15.15L18.725 5.975L20.15 7.4L9.55 18Z" />
        </svg>
        <WarningIcon v-else-if="riskyValues.includes(o.value)" class="risk-icon" :size="14" />
        {{ o.label }}
      </button>
    </div>
    <p v-else class="text-sm text-on-surface-variant mt-4">{{ emptyText }}</p>
  </section>
</template>

<script setup>
import WarningIcon from '@/components/icons/Warning.vue'

defineProps({
  title: { type: String, required: true },
  description: { type: String, default: '' },
  icon: { type: Object, required: true },
  shape: { type: String, default: 'shape-cookie9' },
  tone: { type: String, default: 'bg-primary-container text-on-primary-container' },
  // [{ value, label }]
  options: { type: Array, default: () => [] },
  modelValue: { type: String, default: '' },
  riskyValues: { type: Array, default: () => [] },
  emptyText: { type: String, default: '' },
})
defineEmits(['select'])
</script>

<style scoped>
.picker {
  padding: 20px;
}

.badge {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  min-height: 36px;
  padding: 0 14px;
  border-radius: 10px;
  font-size: 13px;
  font-weight: 600;
  color: var(--color-on-surface-variant);
  background: var(--color-surface-container-highest);
  transition:
    border-radius var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial),
    background-color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects),
    color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.chip.on {
  border-radius: 999px;
  background: var(--color-primary);
  color: var(--color-on-primary);
}

.chip.risky:not(.on) .risk-icon {
  color: var(--color-error);
}

.check {
  width: 16px;
  height: 16px;
  fill: currentColor;
}
</style>
