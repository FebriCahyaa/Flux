<template>
  <!--
    A boolean setting. `main` is the page's primary switch (tonal card that
    changes colour with the state); `row` is one item in an md3-list group.
    The switch is named by the visible title and described by the visible
    description, so screen readers read the same words sighted users see.
  -->
  <div v-if="variant === 'main'" class="main-switch" :class="[{ on: modelValue }, `tone-${tone}`]">
    <div class="flex-1 min-w-0">
      <h2 :id="titleId" class="text-base font-semibold">{{ title }}</h2>
      <p v-if="description" :id="descId" class="text-xs mt-1 opacity-80">{{ description }}</p>
    </div>
    <ToggleSwitch
      :model-value="modelValue"
      :busy="busy"
      :disabled="disabled"
      :labelledby="titleId"
      :describedby="description ? descId : ''"
      @update:model-value="(v) => emit('update:modelValue', v)"
    />
  </div>

  <div v-else class="md3-list">
    <div class="md3-list-item flex items-center gap-4 px-5 py-4 cursor-default">
      <span v-if="icon" class="row-badge" :class="[shape, badgeTone]" aria-hidden="true">
        <component :is="icon" :size="20" />
      </span>
      <span class="flex-1 min-w-0">
        <span class="flex flex-wrap items-center gap-x-2 gap-y-1">
          <span :id="titleId" class="text-sm font-semibold text-on-surface">{{ title }}</span>
          <slot name="tag" />
        </span>
        <span v-if="description" :id="descId" class="block text-xs text-on-surface-variant mt-1 leading-relaxed">{{
          description
        }}</span>
        <slot />
      </span>
      <ToggleSwitch
        :model-value="modelValue"
        :busy="busy"
        :disabled="disabled"
        :labelledby="titleId"
        :describedby="description ? descId : ''"
        @update:model-value="(v) => emit('update:modelValue', v)"
      />
    </div>
  </div>
</template>

<script>
let nextId = 0
</script>

<script setup>
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

defineProps({
  title: { type: String, required: true },
  description: { type: String, default: '' },
  modelValue: { type: Boolean, required: true },
  busy: { type: Boolean, default: false },
  disabled: { type: Boolean, default: false },
  variant: { type: String, default: 'row' }, // 'main' | 'row'
  // main: 'primary' for a normal feature, 'error' when "on" means less protection
  tone: { type: String, default: 'primary' },
  // row badge
  icon: { type: Object, default: null },
  shape: { type: String, default: 'shape-cookie9' },
  badgeTone: { type: String, default: 'bg-secondary-container text-on-secondary-container' },
})
const emit = defineEmits(['update:modelValue'])

const uid = ++nextId
const titleId = `setting-${uid}-title`
const descId = `setting-${uid}-desc`
</script>

<style scoped>
.main-switch {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 20px 20px 20px 24px;
  margin-bottom: 24px;
  border-radius: 28px;
  background: var(--color-surface-container-high);
  color: var(--color-on-surface);
  transition:
    background-color var(--m3-spring-default-effects-duration) var(--m3-spring-default-effects),
    border-radius var(--m3-spring-default-spatial-duration) var(--m3-spring-default-spatial);
}

.main-switch.on {
  border-radius: 36px;
}

.main-switch.on.tone-primary {
  background: var(--color-primary-container);
  color: var(--color-on-primary-container);
}

.main-switch.on.tone-error {
  background: var(--color-error-container);
  color: var(--color-on-error-container);
}

.row-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

@media (prefers-reduced-motion: reduce) {
  .main-switch {
    transition: none;
  }
}
</style>
