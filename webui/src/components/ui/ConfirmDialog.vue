<template>
  <!-- Confirm dialog driven by the notify store: info, warning or danger tone. -->
  <Transition name="dialog">
    <div
      v-if="d"
      class="scrim fixed inset-0 z-[70] flex items-center justify-center p-6"
      @click="notify.answer(false)"
    >
      <div
        class="dialog w-full max-w-sm"
        :class="`tone-${d.tone}`"
        role="alertdialog"
        aria-modal="true"
        :aria-labelledby="'confirm-title'"
        @click.stop
      >
        <span class="hero" :class="tone.shape">
          <component :is="tone.icon" :size="28" />
        </span>
        <h2 id="confirm-title" class="m3-headline text-2xl text-on-surface text-center">
          {{ d.title }}
        </h2>
        <p
          v-if="d.message"
          class="text-sm text-on-surface-variant text-center leading-relaxed mt-3"
        >
          {{ d.message }}
        </p>

        <ul v-if="d.points?.length" class="points mt-4">
          <li v-for="(p, i) in d.points" :key="i" class="flex gap-3 text-sm text-on-surface">
            <span class="dot" />
            <span class="flex-1 leading-snug">{{ p }}</span>
          </li>
        </ul>

        <label v-if="d.remember" class="flex items-center gap-1 mt-4 -ms-2 cursor-pointer">
          <Checkbox v-model="dontAsk" />
          <span class="text-sm text-on-surface-variant">{{ $t('notify.dont_ask_again') }}</span>
        </label>

        <div class="actions mt-6">
          <button
            v-if="d.cancelText !== null"
            class="btn btn-text m3-press"
            @click="notify.answer(false)"
          >
            {{ d.cancelText || $t('common.cancel') }}
          </button>
          <button
            class="btn btn-filled m3-press m3-press-morph"
            @click="notify.answer(true, dontAsk)"
          >
            {{ d.confirmText || $t('common.ok') }}
          </button>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { useNotifyStore } from '@/stores/Notify'
import Checkbox from '@/components/ui/Checkbox.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import WarningIcon from '@/components/icons/Warning.vue'
import ErrorIcon from '@/components/icons/Error.vue'

const notify = useNotifyStore()
const d = computed(() => notify.dialog)
const dontAsk = ref(false)
watch(d, () => (dontAsk.value = false))

const tones = {
  info: { icon: InformationOutlineIcon, shape: 'shape-cookie9' },
  warning: { icon: WarningIcon, shape: 'shape-sunny' },
  danger: { icon: ErrorIcon, shape: 'shape-burst' },
}
const tone = computed(() => tones[d.value?.tone] || tones.info)
</script>

<style scoped>
.scrim {
  background: rgb(0 0 0 / 0.5);
}

.dialog {
  --tone: var(--color-primary);
  --tone-container: var(--color-primary-container);
  --on-tone-container: var(--color-on-primary-container);
  --on-tone: var(--color-on-primary);
  background: var(--color-surface-container-high);
  border-radius: 28px;
  padding: 24px;
  max-height: 85vh;
  overflow-y: auto;
  box-shadow: 0 12px 32px rgb(0 0 0 / 0.3);
}

.tone-warning {
  --tone: var(--color-tertiary);
  --tone-container: var(--color-tertiary-container);
  --on-tone-container: var(--color-on-tertiary-container);
  --on-tone: var(--color-on-tertiary);
}

.tone-danger {
  --tone: var(--color-error);
  --tone-container: var(--color-error-container);
  --on-tone-container: var(--color-on-error-container);
  --on-tone: var(--color-on-error);
}

.hero {
  width: 60px;
  height: 60px;
  display: grid;
  place-items: center;
  margin: 0 auto 16px;
  background: var(--tone-container);
  color: var(--on-tone-container);
}

.points {
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 14px 16px;
  border-radius: 20px;
  background: var(--color-surface-container-low);
}

.dot {
  width: 8px;
  height: 8px;
  margin-top: 6px;
  flex-shrink: 0;
  border-radius: 3px;
  transform: rotate(45deg);
  background: var(--tone);
}

.actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}

.btn {
  min-height: 40px;
  padding: 0 20px;
  border-radius: 999px;
  font-size: 14px;
  font-weight: 650;
}

.btn-text {
  color: var(--tone);
}

.btn-filled {
  background: var(--tone);
  color: var(--on-tone);
}

.dialog-enter-active,
.dialog-leave-active {
  transition: opacity var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.dialog-enter-active .dialog {
  transition: transform var(--m3-spring-default-spatial-duration) var(--m3-spring-default-spatial);
}

.dialog-leave-active .dialog {
  transition: transform var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial);
}

.dialog-enter-from,
.dialog-leave-to {
  opacity: 0;
}

.dialog-enter-from .dialog {
  transform: scale(0.85);
}

.dialog-leave-to .dialog {
  transform: scale(0.95);
}

@media (prefers-reduced-motion: reduce) {
  .dialog-enter-active,
  .dialog-leave-active,
  .dialog-enter-active .dialog,
  .dialog-leave-active .dialog {
    transition: none;
  }
}
</style>
