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
        <div class="flex items-center gap-4 mt-8 mb-5 px-1">
          <span class="hero-badge shape-flower bg-secondary-container text-on-secondary-container">
            <LeafIcon :size="28" />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('lite_mode.title') }}</h1>
        </div>

        <div class="aspect-3/2 rounded-[28px] overflow-hidden mb-3">
          <video
            ref="videoElement"
            preload="auto"
            poster="/illustration/lite_mode_poster.avif"
            class="w-full h-full object-cover"
            autoplay
            loop
            muted
            playsinline
            webkit-playsinline
          >
            <source src="/illustration/lite_mode.webm" type="video/webm" />
          </video>
        </div>

        <!-- Main switch -->
        <div class="switch-card mb-6" :class="{ on: isLiteModeEnabled }">
          <div class="flex-1 min-w-0">
            <h2 class="text-base font-semibold">{{ $t('lite_mode.toggle_title') }}</h2>
            <p class="text-xs mt-1 opacity-80">
              {{ isLiteModeEnabled ? $t('lite_mode.state_on') : $t('lite_mode.state_off') }}
            </p>
          </div>
          <ToggleSwitch
            id="lite-mode"
            :model-value="isLiteModeEnabled"
            @update:modelValue="toggleLiteMode"
          />
        </div>

        <h2 class="text-sm font-semibold text-primary px-3 mb-2">
          {{ $t('lite_mode.effects_title') }}
        </h2>
        <div class="mb-6">
          <div
            v-for="(e, i) in effects"
            :key="e.key"
            class="md3-list m3-enter"
            :style="{ animationDelay: `${i * 50}ms` }"
          >
            <div class="md3-list-item flex items-center gap-4 px-5 py-4">
              <span class="item-badge" :class="[e.shape, e.tone]"
                ><component :is="e.icon" :size="20"
              /></span>
              <span class="flex-1 min-w-0">
                <span class="block text-sm font-semibold text-on-surface">{{
                  $t(`lite_mode.effects.${e.key}.title`)
                }}</span>
                <span class="block text-xs text-on-surface-variant mt-1">{{
                  $t(`lite_mode.effects.${e.key}.description`)
                }}</span>
              </span>
            </div>
          </div>
        </div>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">{{ $t('lite_mode.brief') }}</p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onActivated, onDeactivated, onBeforeUnmount } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import LeafIcon from '@/components/icons/Leaf.vue'
import ThermostatIcon from '@/components/icons/Thermostat.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import SpeedIcon from '@/components/icons/Speed.vue'
import GamesIcon from '@/components/icons/Games.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const isLiteModeEnabled = ref(false)
const videoElement = ref(null)
const wakeLock = ref(null)

const effects = [
  {
    key: 'cooler',
    icon: ThermostatIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'battery',
    icon: BatterySaverIcon,
    shape: 'shape-pentagon',
    tone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'performance',
    icon: SpeedIcon,
    shape: 'shape-clover4',
    tone: 'bg-tertiary-container text-on-tertiary-container',
  },
  {
    key: 'per_game',
    icon: GamesIcon,
    shape: 'shape-cookie6',
    tone: 'bg-surface-container-highest text-on-surface',
  },
]

const playVideo = async () => {
  if (!videoElement.value) return

  try {
    videoElement.value.muted = true
    await videoElement.value.play()
  } catch (err) {
    console.warn('Initial playback failed, retrying after transition...', err)

    setTimeout(async () => {
      try {
        if (videoElement.value) {
          await videoElement.value.play()
        }
      } catch (retryErr) {
        console.error('Playback blocked by WebView policy:', retryErr)
      }
    }, 300)
  }
}

const requestWakeLock = async () => {
  if ('wakeLock' in navigator) {
    try {
      wakeLock.value = await navigator.wakeLock.request('screen')
    } catch (err) {
      console.error(`${err.name}, ${err.message}`)
    }
  }
}

const releaseWakeLock = async () => {
  if (wakeLock.value !== null) {
    await wakeLock.value.release()
    wakeLock.value = null
  }
}

const handleVisibilityChange = async () => {
  if (document.visibilityState === 'visible') {
    await requestWakeLock()
    playVideo()
  }
}

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) {
      await fluxConfigStore.loadConfig()
    }
    isLiteModeEnabled.value = fluxConfigStore.isLiteModeEnabled

    if (videoElement.value) {
      videoElement.value.addEventListener('canplay', playVideo, { once: true })
    }

    document.addEventListener('visibilitychange', handleVisibilityChange)
    await requestWakeLock()
  } catch (error) {
    console.error('Failed to load lite mode setting:', error)
  }
})

onActivated(async () => {
  await requestWakeLock()
  playVideo()
})

onDeactivated(async () => {
  await releaseWakeLock()
  if (videoElement.value) {
    videoElement.value.pause()
  }
})

onBeforeUnmount(async () => {
  document.removeEventListener('visibilitychange', handleVisibilityChange)
  if (videoElement.value) {
    videoElement.value.removeEventListener('canplay', playVideo)
  }
  await releaseWakeLock()
})

// Saved right away; fluxd applies it the next time a game starts.
async function toggleLiteMode(enabled) {
  if (enabled) {
    const ok = await notify.confirm({
      tone: 'info',
      title: t('lite_mode.confirm.title'),
      message: t('lite_mode.confirm.message'),
      points: [t('lite_mode.confirm.point_all_games'), t('lite_mode.confirm.point_fps')],
      confirmText: t('lite_mode.confirm.action'),
    })
    if (!ok) return
  }
  isLiteModeEnabled.value = enabled
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setLiteMode(enabled)
    await fluxConfigStore.saveConfig()
    notify.success(enabled ? t('lite_mode.saved_on') : t('lite_mode.saved_off'))
  } catch (error) {
    console.error('Failed to set lite mode:', error)
    isLiteModeEnabled.value = fluxConfigStore.isLiteModeEnabled
    notify.error(t('notify.save_failed'))
  }
}

function goBack() {
  router.back()
}
</script>

<style scoped>
.hero-badge {
  width: 56px;
  height: 56px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.item-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.switch-card {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 20px 20px 20px 24px;
  border-radius: 28px;
  background: var(--color-surface-container-high);
  color: var(--color-on-surface);
  transition:
    background-color var(--m3-spring-default-effects-duration) var(--m3-spring-default-effects),
    border-radius var(--m3-spring-default-spatial-duration) var(--m3-spring-default-spatial);
}

.switch-card.on {
  border-radius: 36px;
  background: var(--color-primary-container);
  color: var(--color-on-primary-container);
}
</style>
