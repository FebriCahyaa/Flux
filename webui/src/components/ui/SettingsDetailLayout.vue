<template>
  <!--
    Shared shell for every Settings sub-page: back bar, title, short
    description, then the page's own content. Keeping it in one place is what
    makes the deep settings read as one system instead of eleven custom pages.
  -->
  <div class="page h-full flex flex-col overflow-hidden bg-surface">
    <div class="max-w-3xl mx-auto h-full flex flex-col w-full">
      <div class="flex-none px-5 pt-5 pb-2">
        <button
          type="button"
          class="back m3-press w-10 h-10 -ms-2 rounded-full grid place-items-center text-on-surface hover:bg-surface-container-high"
          :aria-label="$t('common.back')"
          @click="router.back()"
        >
          <ArrowLeftIcon class="w-6 h-6 rtl:rotate-180" aria-hidden="true" />
        </button>
      </div>

      <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll px-4">
        <div class="flex items-center gap-4 mt-4 mb-2 px-1">
          <span v-if="icon" class="title-badge" :class="[shape, tone]" aria-hidden="true">
            <component :is="icon" :size="24" />
          </span>
          <h1 class="m3-headline text-[32px] text-on-surface min-w-0 flex-1 break-words">
            {{ title }}
          </h1>
          <slot name="title-aside" />
        </div>
        <p v-if="description" class="text-sm text-on-surface-variant leading-relaxed px-1 mb-5">
          {{ description }}
        </p>
        <div v-else class="mb-4"></div>

        <div
          v-if="readError"
          class="flex items-start gap-3 rounded-[20px] px-4 py-3 mb-4 bg-error-container text-on-error-container"
          role="alert"
        >
          <WarningIcon class="shrink-0 mt-0.5" :size="18" aria-hidden="true" />
          <p class="text-xs leading-relaxed">{{ $t('notify.read_failed') }}</p>
        </div>

        <slot />
      </div>
    </div>
  </div>
</template>

<script setup>
import { useRouter } from 'vue-router'
import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import WarningIcon from '@/components/icons/Warning.vue'

defineProps({
  title: { type: String, required: true },
  description: { type: String, default: '' },
  icon: { type: Object, default: null },
  shape: { type: String, default: 'shape-cookie9' },
  tone: { type: String, default: 'bg-secondary-container text-on-secondary-container' },
  // The page could not read its current state (config.json / sysfs).
  readError: { type: Boolean, default: false },
})

const router = useRouter()
</script>

<style scoped>
.title-badge {
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.back:focus-visible {
  outline: 2px solid var(--color-primary);
  outline-offset: 2px;
}
</style>
