<template>
  <div class="page home-page h-full flex flex-col">
    <!-- Header -->
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-3xl mx-auto px-5 pt-6 pb-3 flex items-end justify-between gap-3">
        <h1 class="m3-headline text-[32px] text-on-surface">{{ $t('home_page.title') }}</h1>
        <span
          class="allow-copy shrink-0 rounded-full bg-surface-container-high px-3 py-1 text-xs font-medium text-on-surface-variant"
        >
          {{ shortVersion }}
        </span>
      </div>
    </div>

    <!-- Scrollable Content -->
    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll">
      <div class="max-w-3xl mx-auto px-4 py-1">
        <!-- Hero: daemon status and active profile -->
        <section
          class="hero m3-enter relative overflow-hidden rounded-[32px] p-5 mb-3"
          :class="heroTone.container"
        >
          <span class="hero-deco shape-burst" :class="heroTone.deco" aria-hidden="true"></span>

          <div class="relative flex items-center gap-4">
            <div
              class="mascot shape-cookie12 shrink-0 grid place-items-center"
              :class="heroTone.mascot"
            >
              <img :src="homeStore.logoImage" class="w-24 h-24 object-contain" alt="Flux" />
            </div>
            <div class="min-w-0 flex-1">
              <span
                class="status-chip inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-semibold"
                :class="heroTone.chip"
              >
                <span class="dot"></span>{{ statusChipText }}
              </span>
              <h2 class="m3-headline text-2xl mt-2">{{ daemonStatusText }}</h2>
              <p class="text-xs mt-1 opacity-80">{{ daemonPidText }}</p>
            </div>
          </div>

          <!-- Connected button group: the active profile is highlighted (profiles switch automatically) -->
          <div class="relative mt-5">
            <p class="text-xs font-semibold opacity-80 mb-2 px-1">
              {{ $t('home_page.info_card.profile') }}
            </p>
            <div class="profile-group flex gap-0.5" role="list">
              <span
                v-for="p in profiles"
                :key="p"
                role="listitem"
                class="profile-seg flex-1 text-center text-xs font-semibold py-2.5 truncate"
                :class="
                  p === activeProfile ? 'is-active bg-primary text-on-primary' : heroTone.segment
                "
                :aria-current="p === activeProfile ? 'true' : undefined"
              >
                {{ $t(`home_page.profile_short.${p}`) }}
              </span>
            </div>
          </div>
        </section>

        <!-- Bento: device facts -->
        <section class="grid grid-cols-2 gap-2 mb-3">
          <div
            class="bento m3-enter col-span-2 bg-surface-container rounded-[28px] p-4 flex items-center gap-4"
            style="animation-delay: 40ms"
          >
            <span class="badge shape-cookie6 bg-secondary-container text-on-secondary-container"
              ><ConsoleIcon
            /></span>
            <div class="min-w-0">
              <p class="text-xs text-on-surface-variant">{{ $t('home_page.info_card.kernel') }}</p>
              <p class="allow-copy text-sm font-semibold text-on-surface break-all">
                {{ displayValue(homeStore.kernelVersion) }}
              </p>
            </div>
          </div>

          <div
            class="bento m3-enter bg-tertiary-container text-on-tertiary-container rounded-[28px] p-4"
            style="animation-delay: 80ms"
          >
            <span class="badge shape-pentagon bg-tertiary text-on-tertiary mb-6"
              ><ChipsetIcon
            /></span>
            <p class="text-xs opacity-80">{{ $t('home_page.info_card.chipset') }}</p>
            <p class="allow-copy m3-headline text-lg break-words">
              {{ displayValue(homeStore.chipsetName) }}
            </p>
          </div>

          <div
            class="bento m3-enter bg-secondary-container text-on-secondary-container rounded-[28px] p-4"
            style="animation-delay: 120ms"
          >
            <span class="badge shape-clover4 bg-secondary text-on-secondary mb-6"
              ><AndroidIcon
            /></span>
            <p class="text-xs opacity-80">{{ $t('home_page.info_card.androidSDK') }}</p>
            <p class="allow-copy m3-headline text-4xl">{{ displayValue(homeStore.androidSDK) }}</p>
          </div>

          <div
            class="bento m3-enter col-span-2 bg-surface-container rounded-[28px] p-4 flex items-center gap-4"
            style="animation-delay: 160ms"
          >
            <span class="badge shape-sunny bg-primary-container text-on-primary-container"
              ><StarIcon
            /></span>
            <div class="min-w-0">
              <p class="text-xs text-on-surface-variant">{{ $t('home_page.info_card.module') }}</p>
              <p class="allow-copy text-sm font-semibold text-on-surface break-all">
                {{ displayValue(homeStore.moduleVersion) }}
              </p>
            </div>
          </div>
        </section>

        <!-- Links -->
        <section class="m3-enter mb-4" style="animation-delay: 200ms">
          <div class="md3-list">
            <RippleComponent @click="handleDonateClick" tabindex="0" class="md3-list-item">
              <div class="flex items-center gap-4 px-5 py-4">
                <span class="badge-sm shape-flower bg-tertiary-container text-on-tertiary-container"
                  ><StarlyGear
                /></span>
                <div class="min-w-0 flex-1">
                  <h3 class="text-sm font-semibold text-on-surface">
                    {{ $t('home_page.support_button.title') }}
                  </h3>
                  <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">
                    {{ $t('home_page.support_button.description') }}
                  </p>
                </div>
                <ChevronRightIcon
                  class="text-on-surface-variant shrink-0 rtl:rotate-180"
                  :size="22"
                />
              </div>
            </RippleComponent>
          </div>
          <div class="md3-list">
            <RippleComponent @click="handleGuideClick" tabindex="0" class="md3-list-item">
              <div class="flex items-center gap-4 px-5 py-4">
                <span
                  class="badge-sm shape-cookie9 bg-secondary-container text-on-secondary-container"
                  ><InformationOutlineIcon :size="20"
                /></span>
                <div class="min-w-0 flex-1">
                  <h3 class="text-sm font-semibold text-on-surface">
                    {{ $t('home_page.learn_flux.title') }}
                  </h3>
                  <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">
                    {{ $t('home_page.learn_flux.description') }}
                  </p>
                </div>
                <ChevronRightIcon
                  class="text-on-surface-variant shrink-0 rtl:rotate-180"
                  :size="22"
                />
              </div>
            </RippleComponent>
          </div>
        </section>
      </div>
    </div>
  </div>
</template>

<script setup>
import { onMounted, onUnmounted, computed } from 'vue'
import { useHomeStore } from '@/stores/Home'
import * as KernelSU from '@/helpers/KernelSU'
import { useI18n } from 'vue-i18n'

import RippleComponent from '@/components/ui/Ripple.vue'
import StarIcon from '@/components/icons/Star.vue'
import StarlyGear from '@/components/icons/StarlyGear.vue'
import ConsoleIcon from '@/components/icons/Console.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import AndroidIcon from '@/components/icons/Android.vue'
import ChevronRightIcon from '@/components/icons/ChevronRight.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'

const { t } = useI18n()
const homeStore = useHomeStore()

// Helper function to display values with proper i18n
function displayValue(value) {
  if (value === 'unknown' || !value) {
    return t('common.unknown')
  }
  return value
}

// Computed properties for translated text
const daemonStatusText = computed(() => {
  const status = homeStore.daemonStatusRaw
  if (status === 'loading') return t('common.loading')
  return t(`home_page.status_card.${status}`)
})

const daemonPidText = computed(() => {
  const status = homeStore.daemonStatusRaw
  if (status === 'running' && homeStore.daemonPidRaw) {
    return t('home_page.status_card.daemonPID', { pid: homeStore.daemonPidRaw })
  } else if (status === 'stopped') {
    return t('home_page.status_card.daemon_inactive')
  } else if (status === 'error' && homeStore.daemonError) {
    return homeStore.daemonError
  }
  return t('home_page.status_card.loading_daemon')
})

const currentProfileText = computed(() => {
  const profileKey = homeStore.currentProfileRaw
  if (profileKey === 'unknown' || !profileKey) return t('common.unknown')

  // Check if translation exists, fallback to raw key
  const translation = t(`profiles.${profileKey}`)
  return translation !== `profiles.${profileKey}` ? translation : profileKey
})

const profiles = ['performance', 'performance_lite', 'balanced', 'powersave']
const activeProfile = computed(() => homeStore.currentProfileRaw)

const shortVersion = computed(() => {
  const v = homeStore.moduleVersion
  if (!v || v === 'unknown') return 'Flux'
  return `v${v.split(' ')[0]}`
})

const statusChipText = computed(() => {
  const status = homeStore.daemonStatusRaw
  if (status === 'running') return t('home_page.status_chip.running')
  if (status === 'stopped' || status === 'error') return t('home_page.status_chip.stopped')
  return t('common.loading')
})

// Colour roles follow the daemon state: primary while running, error when stopped.
const heroTone = computed(() => {
  const status = homeStore.daemonStatusRaw
  if (status === 'stopped' || status === 'error') {
    return {
      container: 'bg-error-container text-on-error-container',
      deco: 'bg-error',
      mascot: 'bg-error/25',
      chip: 'bg-error text-on-error',
      segment: 'bg-on-error-container/10',
    }
  }
  return {
    container: 'bg-primary-container text-on-primary-container',
    deco: 'bg-primary',
    mascot: 'bg-tertiary-container',
    chip: 'bg-on-primary-container text-primary-container',
    segment: 'bg-on-primary-container/10',
  }
})

onMounted(async () => {
  await homeStore.initializeData()
})

onUnmounted(() => {
  homeStore.stopProfileMonitoring()
  homeStore.stopDaemonMonitoring()
})

function handleGuideClick() {
  KernelSU.openWebsite('')
}

function handleDonateClick() {
  KernelSU.openWebsite('https://t.me/c/3901105851/3')
}
</script>

<style scoped>
.hero-deco {
  position: absolute;
  width: 220px;
  height: 220px;
  right: -70px;
  top: -80px;
  opacity: 0.16;
  animation: hero-spin 30s linear infinite;
}

.mascot {
  width: 112px;
  height: 112px;
  animation: mascot-breathe 5s var(--m3-spring-default-spatial) infinite alternate;
}

.status-chip .dot {
  width: 6px;
  height: 6px;
  border-radius: 999px;
  background: currentColor;
  animation: dot-pulse 1.6s ease-in-out infinite;
}

.profile-seg {
  border-radius: 8px;
  transition:
    border-radius var(--m3-spring-default-spatial-duration) var(--m3-spring-fast-spatial),
    background-color var(--m3-spring-default-effects-duration) var(--m3-spring-default-effects),
    flex-grow var(--m3-spring-default-spatial-duration) var(--m3-spring-fast-spatial);
}

.profile-seg:first-child {
  border-radius: 20px 8px 8px 20px;
}

.profile-seg:last-child {
  border-radius: 8px 20px 20px 8px;
}

.profile-seg.is-active {
  border-radius: 999px;
  flex-grow: 1.35;
}

.badge,
.badge-sm {
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.badge {
  width: 48px;
  height: 48px;
}

.badge-sm {
  width: 40px;
  height: 40px;
}

.bento {
  transition: transform var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial);
}

.bento:active {
  transform: scale(0.97);
}

@keyframes hero-spin {
  to {
    transform: rotate(1turn);
  }
}

@keyframes mascot-breathe {
  to {
    clip-path: var(--m3-shape-cookie9);
    transform: rotate(8deg);
  }
}

@keyframes dot-pulse {
  50% {
    opacity: 0.35;
  }
}

@media (prefers-reduced-motion: reduce) {
  .hero-deco,
  .mascot,
  .status-chip .dot {
    animation: none;
  }
}
</style>
