<template>
  <!--
    Material 3 Expressive switch. The handle grows from 16dp (off) to 24dp (on)
    and 28dp while pressed, carries an icon in both states and moves on the
    fast spatial spring, so it overshoots slightly like the Android component.
  -->
  <label
    class="m3-switch relative inline-flex cursor-pointer items-center align-middle select-none"
    :class="{ 'is-on': modelValue, 'is-pressed': pressed, 'is-disabled': disabled }"
    @pointerdown="pressed = true"
    @pointerup="pressed = false"
    @pointerleave="pressed = false"
    @pointercancel="pressed = false"
  >
    <input
      :id="switchId"
      type="checkbox"
      role="switch"
      class="peer sr-only"
      :checked="modelValue"
      :aria-checked="modelValue"
      @change="handleChange"
      :disabled="disabled"
    />
    <span class="track">
      <span class="handle">
        <svg class="icon icon-on" viewBox="0 0 24 24" aria-hidden="true">
          <path d="M9.55 18L3.85 12.3L5.275 10.875L9.55 15.15L18.725 5.975L20.15 7.4L9.55 18Z" />
        </svg>
        <svg class="icon icon-off" viewBox="0 0 24 24" aria-hidden="true">
          <path
            d="M6.4 19L5 17.6L10.6 12L5 6.4L6.4 5L12 10.6L17.6 5L19 6.4L13.4 12L19 17.6L17.6 19L12 13.4Z"
          />
        </svg>
      </span>
    </span>
  </label>
</template>

<script setup>
import { ref } from 'vue'

const props = defineProps({
  modelValue: {
    type: Boolean,
    required: true,
  },
  disabled: {
    type: Boolean,
    default: false,
  },
  id: {
    type: String,
    default: '',
  },
})

const emit = defineEmits(['update:modelValue'])

let defaultId = 0
const switchId = props.id || `toggle-switch-${++defaultId}`
const pressed = ref(false)

// The switch only shows modelValue: the native checkbox is put back until the
// parent accepts the change, so a cancelled confirm dialog leaves it untouched.
function handleChange(event) {
  const next = event.target.checked
  event.target.checked = props.modelValue
  emit('update:modelValue', next)
}
</script>

<style scoped>
.m3-switch {
  --track-w: 52px;
  --track-h: 32px;
  --size: 16px;
  --x: 8px;
}

.m3-switch.is-on {
  --size: 24px;
  --x: calc(var(--track-w) - 24px - 4px);
}

.m3-switch.is-pressed {
  --size: 28px;
  --x: 2px;
}

.m3-switch.is-on.is-pressed {
  --x: calc(var(--track-w) - 28px - 2px);
}

.m3-switch.is-disabled {
  opacity: 0.38;
  pointer-events: none;
}

.track {
  position: relative;
  display: block;
  width: var(--track-w);
  height: var(--track-h);
  border-radius: 999px;
  border: 2px solid var(--color-outline);
  background: var(--color-surface-container-highest);
  transition:
    background-color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects),
    border-color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.is-on .track {
  background: var(--color-primary);
  border-color: var(--color-primary);
}

.handle {
  position: absolute;
  top: 50%;
  left: -2px;
  width: var(--size);
  height: var(--size);
  border-radius: 999px;
  display: grid;
  place-items: center;
  background: var(--color-outline);
  color: var(--color-surface-container-highest);
  transform: translate(var(--x), -50%);
  transition:
    transform var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial),
    width var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial),
    height var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial),
    background-color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.is-on .handle {
  background: var(--color-on-primary);
  color: var(--color-on-primary-container);
}

.is-pressed:not(.is-on) .handle {
  background: var(--color-on-surface-variant);
}

.is-pressed.is-on .handle {
  background: var(--color-primary-container);
}

/* State layer halo on press / keyboard focus */
.handle::before {
  content: '';
  position: absolute;
  inset: -8px;
  border-radius: 999px;
  background: currentColor;
  opacity: 0;
  transition: opacity var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.m3-switch:hover .handle::before {
  opacity: 0.08;
}

.is-pressed .handle::before,
.peer:focus-visible + .track .handle::before {
  opacity: 0.12;
}

.icon {
  position: absolute;
  width: 16px;
  height: 16px;
  fill: currentColor;
  transition:
    opacity var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects),
    transform var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial);
}

.icon-on {
  opacity: 0;
  transform: scale(0.4) rotate(-45deg);
}

.icon-off {
  opacity: 0;
  transform: scale(0.4);
}

.is-on .icon-on {
  opacity: 1;
  transform: none;
}

/* The off icon only shows once the handle is large enough (pressed). */
.is-pressed:not(.is-on) .icon-off {
  opacity: 1;
  transform: none;
  color: var(--color-surface-container-highest);
}

.peer:focus-visible + .track {
  outline: 2px solid var(--color-secondary);
  outline-offset: 2px;
}

@media (prefers-reduced-motion: reduce) {
  .track,
  .handle,
  .icon {
    transition: none;
  }
}
</style>
