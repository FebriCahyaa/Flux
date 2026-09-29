<template>
  <!-- About is a special screen: the shared shell gives it the same back bar
       and title as every other settings page; the content stays its own. -->
  <SettingsDetailLayout :title="$t('settings_page.about.title')">
    <template #title-aside>
      <span
        class="allow-copy shrink-0 rounded-full bg-surface-container-high px-3 py-1 text-xs font-medium text-on-surface-variant"
      >
        {{ versionText }}
      </span>
    </template>

        <!-- Developer card (M3 Expressive) -->
        <section
          class="about-card m3-enter bg-surface-container-high rounded-[32px] p-5 mb-3 relative overflow-hidden"
        >
          <span class="about-deco shape-flower bg-primary" aria-hidden="true"></span>

          <div class="relative flex items-center gap-4">
            <div
              class="about-avatar shape-cookie12 bg-primary-container text-on-primary-container grid place-items-center shrink-0"
              aria-hidden="true"
            >
              <CodeIcon :size="30" />
            </div>
            <div class="min-w-0">
              <h2 class="m3-headline text-2xl text-on-surface">FebriCahyaa</h2>
              <p class="text-xs text-on-surface-variant mt-1">
                {{ $t('settings_page.about.role') }}
              </p>
              <span
                class="mt-2 inline-flex items-center gap-1.5 rounded-full bg-primary text-on-primary px-2.5 py-1 text-xs font-semibold"
              >
                <span class="about-dot" aria-hidden="true"></span>{{ $t('settings_page.about.status') }}
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

          <!-- Button group: tonal + filled, pills that morph on press. They open an
               external page, so they are announced as links. -->
          <div class="relative flex gap-2 mt-5">
            <RippleComponent
              role="link"
              @click="openGithub"
              class="about-link m3-press m3-press-morph flex-1 flex items-center justify-center gap-2 rounded-full bg-secondary-container text-on-secondary-container py-3.5 cursor-pointer"
              tabindex="0"
            >
              <GithubIcon :size="18" aria-hidden="true" />
              <span class="text-sm font-semibold">GitHub</span>
            </RippleComponent>
            <RippleComponent
              role="link"
              @click="openTelegram"
              class="about-link m3-press m3-press-morph flex-1 flex items-center justify-center gap-2 rounded-full bg-primary text-on-primary py-3.5 cursor-pointer"
              tabindex="0"
            >
              <TelegramIcon :size="18" aria-hidden="true" />
              <span class="text-sm font-semibold">Telegram</span>
            </RippleComponent>
          </div>
        </section>

        <!-- Module tagline -->
        <div class="text-center py-4 mb-2">
          <p class="m3-headline text-lg text-primary">"{{ $t('settings_page.about.tagline') }}"</p>
          <p class="text-xs text-on-surface-variant opacity-60 mt-1">Flux Tweaks · FebriCahyaa</p>
        </div>
  </SettingsDetailLayout>
</template>

<script setup>
import { computed, onMounted } from 'vue'
import { useHomeStore } from '@/stores/Home'
import * as KernelSU from '@/helpers/KernelSU'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import RippleComponent from '@/components/ui/Ripple.vue'
import CodeIcon from '@/components/icons/Code.vue'
import GithubIcon from '@/components/icons/Github.vue'
import TelegramIcon from '@/components/icons/Telegram.vue'

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

.about-link:focus-visible {
  outline: 2px solid var(--color-primary);
  outline-offset: 2px;
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
