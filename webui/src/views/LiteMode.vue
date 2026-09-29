<template>
  <SettingsDetailLayout
    :title="$t('lite_mode.title')"
    :description="$t('lite_mode.brief')"
    :icon="LeafIcon"
    shape="shape-flower"
    tone="bg-secondary-container text-on-secondary-container"
    :read-error="readError"
  >
    <!-- Decorative illustration: static poster when reduced motion is on. -->
    <div class="illustration rounded-[28px] overflow-hidden mb-4" aria-hidden="true">
      <video
        ref="videoElement"
        preload="auto"
        poster="/illustration/lite_mode_poster.avif"
        class="w-full h-full object-cover"
        :autoplay="!reducedMotion"
        :loop="!reducedMotion"
        muted
        playsinline
        webkit-playsinline
      >
        <source src="/illustration/lite_mode.webm" type="video/webm" />
      </video>
    </div>

    <SettingsSwitch
      variant="main"
      :title="$t('lite_mode.toggle_title')"
      :description="isLiteModeEnabled ? $t('lite_mode.state_on') : $t('lite_mode.state_off')"
      :model-value="isLiteModeEnabled"
      :busy="saving"
      :disabled="!ready"
      @update:model-value="toggleLiteMode"
    />

    <h2 class="text-sm font-semibold text-primary px-4 pb-2">
      {{ $t('lite_mode.effects_title') }}
    </h2>
    <div class="mb-6">
      <div v-for="e in effects" :key="e.key" class="md3-list">
        <div class="md3-list-item flex items-center gap-4 px-5 py-4 cursor-default">
          <span class="item-badge" :class="[e.shape, e.tone]" aria-hidden="true"
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
  </SettingsDetailLayout>
</template>

<script setup>
import { ref, onMounted, onActivated, onDeactivated, onBeforeUnmount } from 'vue'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsSwitch from '@/components/ui/SettingsSwitch.vue'
import LeafIcon from '@/components/icons/Leaf.vue'
import ThermostatIcon from '@/components/icons/Thermostat.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import SpeedIcon from '@/components/icons/Speed.vue'
import GamesIcon from '@/components/icons/Games.vue'

const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const isLiteModeEnabled = ref(false)
const ready = ref(false)
const readError = ref(false)
const saving = ref(false)
const videoElement = ref(null)
const wakeLock = ref(null)

// The illustration loops forever; with reduced motion the poster stays still.
const reducedMotion =
  typeof window !== 'undefined' &&
  window.matchMedia?.('(prefers-reduced-motion: reduce)').matches

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
  if (!videoElement.value || reducedMotion) return

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
  } catch (error) {
    console.error('Failed to load lite mode setting:', error)
    readError.value = true
  }
  ready.value = true

  if (videoElement.value) {
    videoElement.value.addEventListener('canplay', playVideo, { once: true })
  }
  document.addEventListener('visibilitychange', handleVisibilityChange)
  await requestWakeLock()
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
  if (saving.value) return
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
  saving.value = true
  isLiteModeEnabled.value = enabled
  try {
    await fluxConfigStore.commit(() => fluxConfigStore.setLiteMode(enabled))
    readError.value = false
    notify.success(enabled ? t('lite_mode.saved_on') : t('lite_mode.saved_off'))
  } catch (error) {
    console.error('Failed to set lite mode:', error)
    notify.error(t('notify.save_failed'))
  } finally {
    isLiteModeEnabled.value = fluxConfigStore.isLiteModeEnabled
    saving.value = false
  }
}
</script>

<style scoped>
/* 3:2 on a phone, but never a full-width poster on tablets/desktop. */
.illustration {
  aspect-ratio: 3 / 2;
  max-height: 240px;
  width: 100%;
}

.item-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
</style>
