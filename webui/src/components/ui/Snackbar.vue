<template>
  <!-- Snackbars from the notify store, above the navigation bar. -->
  <div class="snackbar-host" aria-live="polite">
    <TransitionGroup name="snack">
      <div
        v-for="s in notify.snackbars"
        :key="s.id"
        class="snackbar"
        :class="`tone-${s.tone}`"
        role="status"
      >
        <span class="snack-icon"><component :is="icons[s.tone] || icons.info" :size="20" /></span>
        <p class="flex-1 text-sm leading-snug">{{ s.message }}</p>
        <button
          v-if="s.action"
          class="snack-action m3-press"
          @click="(s.action.run(), notify.dismiss(s.id))"
        >
          {{ s.action.label }}
        </button>
        <button
          class="snack-close m3-press"
          :aria-label="$t('common.close')"
          @click="notify.dismiss(s.id)"
        >
          <CloseIcon :size="18" />
        </button>
      </div>
    </TransitionGroup>
  </div>
</template>

<script setup>
import { useNotifyStore } from '@/stores/Notify'
import CloseIcon from '@/components/icons/Close.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import CheckCircleIcon from '@/components/icons/CheckCircle.vue'
import WarningIcon from '@/components/icons/Warning.vue'
import ErrorIcon from '@/components/icons/Error.vue'

const notify = useNotifyStore()
const icons = {
  info: InformationOutlineIcon,
  success: CheckCircleIcon,
  warning: WarningIcon,
  error: ErrorIcon,
}
</script>

<style scoped>
.snackbar-host {
  position: fixed;
  left: 0;
  right: 0;
  bottom: calc(var(--nav-height, 0px) + 12px);
  z-index: 60;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 0 16px;
  pointer-events: none;
}

@media (min-width: 768px) {
  .snackbar-host {
    left: 80px;
    bottom: calc(24px + var(--window-inset-bottom, 0px));
  }
}

.snackbar {
  pointer-events: auto;
  width: 100%;
  max-width: 560px;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 8px 10px 14px;
  border-radius: 16px;
  background: var(--color-inverse-surface);
  color: var(--color-inverse-on-surface);
  box-shadow:
    0 6px 16px rgb(0 0 0 / 0.22),
    0 1px 3px rgb(0 0 0 / 0.18);
}

.snack-icon {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
  border-radius: 999px;
  color: var(--color-inverse-primary);
  background: color-mix(in srgb, var(--color-inverse-primary) 16%, transparent);
}

.tone-warning .snack-icon {
  color: var(--color-on-tertiary-container);
  background: var(--color-tertiary-container);
}

.tone-error .snack-icon {
  color: var(--color-on-error-container);
  background: var(--color-error-container);
}

.snack-action {
  flex-shrink: 0;
  padding: 6px 10px;
  border-radius: 999px;
  font-size: 13px;
  font-weight: 650;
  color: var(--color-inverse-primary);
}

.snack-close {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
  border-radius: 999px;
  opacity: 0.8;
}

.snack-enter-active {
  transition:
    transform var(--m3-spring-default-spatial-duration) var(--m3-spring-default-spatial),
    opacity var(--m3-spring-default-effects-duration) var(--m3-spring-default-effects);
}

.snack-leave-active {
  transition:
    transform var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial),
    opacity var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.snack-enter-from {
  opacity: 0;
  transform: translateY(24px) scale(0.92);
}

.snack-leave-to {
  opacity: 0;
  transform: scale(0.92);
}

@media (prefers-reduced-motion: reduce) {
  .snack-enter-active,
  .snack-leave-active {
    transition: none;
  }
}
</style>
