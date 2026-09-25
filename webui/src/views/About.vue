<template>
  <div class="page h-full flex flex-col overflow-hidden bg-surface">
    <div class="max-w-3xl mx-auto h-full flex flex-col w-full">
      <div class="flex-none p-5 pb-3">
        <button
          @click="goBack"
          class="m3-press w-10 h-10 -ms-2 rounded-full grid place-items-center text-on-surface hover:bg-surface-container-high"
          :aria-label="$t('common.cancel')"
        >
          <ArrowLeftIcon class="w-6 h-6 rtl:rotate-180" />
        </button>
      </div>

      <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll px-4">
        <div class="flex items-end justify-between gap-3 mt-8 mb-6 px-1">
          <h1 class="m3-headline text-4xl text-on-surface">
            {{ $t('settings_page.section.about') }}
          </h1>
          <span
            class="allow-copy shrink-0 rounded-full bg-surface-container-high px-3 py-1 text-xs font-medium text-on-surface-variant"
          >
            {{ versionText }}
          </span>
        </div>

        <!-- Developer card (M3 Expressive) -->
        <section
          class="about-card m3-enter bg-surface-container-high rounded-[32px] p-5 mb-3 relative overflow-hidden"
        >
          <span class="about-deco shape-flower bg-primary" aria-hidden="true"></span>

          <div class="relative flex items-center gap-4">
            <div
              class="about-avatar shape-cookie12 bg-primary-container text-on-primary-container grid place-items-center shrink-0"
            >
              <CodeIcon :size="30" />
            </div>
            <div class="min-w-0">
              <h3 class="m3-headline text-2xl text-on-surface">FebriCahyaa</h3>
              <p class="text-xs text-on-surface-variant mt-1">
                {{ $t('settings_page.about.role') }}
              </p>
              <span
                class="mt-2 inline-flex items-center gap-1.5 rounded-full bg-primary text-on-primary px-2.5 py-1 text-xs font-semibold"
              >
                <span class="about-dot"></span>{{ $t('settings_page.about.status') }}
              </span>
            </div>
          </div>

          <!-- Facts: segmented tonal tiles -->
          <div class="about-facts relative grid grid-cols-2 gap-0.5 mt-5">
            <div class="fact bg-secondary-container text-on-secondary-container">
              <p class="text-xs opacity-80">{{ $t('settings_page.about.module_name') }}</p>
              <p class="text-sm font-bold mt-1">Flux Tweaks</p>
            </div>
            <div class="fact bg-tertiary-container text-on-tertiary-container">
              <p class="text-xs opacity-80">{{ $t('settings_page.about.platform') }}</p>
              <p class="text-sm font-bold mt-1">Magisk · KSU · APatch</p>
            </div>
            <div class="fact bg-surface-container-highest text-on-surface">
              <p class="text-xs text-on-surface-variant">{{ $t('settings_page.about.target') }}</p>
              <p class="text-sm font-bold mt-1">GKI &amp; Non-GKI</p>
            </div>
            <div class="fact bg-primary-container text-on-primary-container">
              <p class="text-xs opacity-80">{{ $t('settings_page.about.license') }}</p>
              <p class="text-sm font-bold mt-1">Apache 2.0</p>
            </div>
          </div>

          <p class="relative text-sm text-on-surface-variant leading-relaxed mt-4 px-1">
            {{ $t('settings_page.about.description') }}
          </p>

          <!-- Button group: tonal + filled, pills that morph on press -->
          <div class="relative flex gap-2 mt-5">
            <RippleComponent
              @click="openGithub"
              class="m3-press m3-press-morph flex-1 flex items-center justify-center gap-2 rounded-full bg-secondary-container text-on-secondary-container py-3.5 cursor-pointer"
              tabindex="0"
            >
              <GithubIcon :size="18" />
              <span class="text-sm font-semibold">GitHub</span>
            </RippleComponent>
            <RippleComponent
              @click="openTelegram"
              class="m3-press m3-press-morph flex-1 flex items-center justify-center gap-2 rounded-full bg-primary text-on-primary py-3.5 cursor-pointer"
              tabindex="0"
            >
              <TelegramIcon :size="18" />
              <span class="text-sm font-semibold">Telegram</span>
            </RippleComponent>
          </div>
        </section>

        <!-- Module tagline -->
        <div class="text-center py-4 mb-2">
          <p class="m3-headline text-lg text-primary">"{{ $t('settings_page.about.tagline') }}"</p>
          <p class="text-xs text-on-surface-variant opacity-60 mt-1">Flux Tweaks · FebriCahyaa</p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useHomeStore } from '@/stores/Home'
import * as KernelSU from '@/helpers/KernelSU'

import RippleComponent from '@/components/ui/Ripple.vue'
import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import CodeIcon from '@/components/icons/Code.vue'
import GithubIcon from '@/components/icons/Github.vue'
import TelegramIcon from '@/components/icons/Telegram.vue'

const router = useRouter()
const homeStore = useHomeStore()

const versionText = computed(() => {
  const v = homeStore.moduleVersion
  return v && v !== 'unknown' ? `v${v.split(' ')[0]}` : 'Flux Tweaks'
})

onMounted(() => {
  if (!homeStore.moduleVersion || homeStore.moduleVersion === 'unknown')
    homeStore.getModuleVersion?.()
})

const openGithub = () => KernelSU.openWebsite('https://github.com/FebriCahyaa/Flux')
const openTelegram = () => KernelSU.openWebsite('https://t.me/c/3901105851/3')

function goBack() {
  router.back()
}
</script>

<style scoped>
.about-deco {
  position: absolute;
  width: 180px;
  height: 180px;
  right: -60px;
  top: -70px;
  opacity: 0.1;
  animation: about-spin 40s linear infinite;
}

.about-avatar {
  width: 72px;
  height: 72px;
  transition: clip-path var(--m3-spring-slow-spatial-duration) var(--m3-spring-default-spatial);
}

.about-card:active .about-avatar {
  clip-path: var(--m3-shape-burst);
}

.about-dot {
  width: 6px;
  height: 6px;
  border-radius: 999px;
  background: currentColor;
  animation: about-pulse 1.6s ease-in-out infinite;
}

/* Segmented 2x2 group: large outer corners, small inner ones */
.about-facts .fact {
  padding: 14px 16px;
  border-radius: 6px;
}

.about-facts .fact:nth-child(1) {
  border-top-left-radius: 24px;
}

.about-facts .fact:nth-child(2) {
  border-top-right-radius: 24px;
}

.about-facts .fact:nth-child(3) {
  border-bottom-left-radius: 24px;
}

.about-facts .fact:nth-child(4) {
  border-bottom-right-radius: 24px;
}

@keyframes about-spin {
  to {
    transform: rotate(1turn);
  }
}

@keyframes about-pulse {
  50% {
    opacity: 0.35;
  }
}

@media (prefers-reduced-motion: reduce) {
  .about-deco,
  .about-dot {
    animation: none;
  }
}
</style>
