<template>
  <div class="page monitor-page h-full flex flex-col">

    <!-- Header -->
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-3xl mx-auto px-5 pt-6 pb-3">
        <div class="flex justify-between items-center text-on-surface">
          <h1 class="m3-headline text-[32px]">{{ $t('monitor_page.title') }}</h1>
          <div class="flex items-center gap-2">
            <span class="text-xs text-on-surface-variant font-medium">{{ $t('monitor_page.live') }}</span>
            <span class="live-dot w-2 h-2 rounded-full" :class="dotClass"></span>
          </div>
        </div>
      </div>
    </div>

    <!-- Scrollable content -->
    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll">
      <div class="max-w-3xl mx-auto p-5 py-1 space-y-3">

        <!-- Error banner -->
        <div
          v-if="monitorStore.lastError"
          class="bg-error-container text-on-error-container rounded-2xl px-4 py-3 text-xs flex items-center gap-2"
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 -960 960 960" fill="currentColor">
            <path d="M480-280q17 0 28.5-11.5T520-320q0-17-11.5-28.5T480-360q-17 0-28.5 11.5T440-320q0 17 11.5 28.5T480-280Zm-40-160h80v-240h-80v240Zm40 360q-83 0-156-31.5T197-197q-54-54-85.5-127T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 83-31.5 156T763-197q-54 54-127 85.5T480-80Z"/>
          </svg>
          {{ monitorStore.lastError }}
        </div>

        <!-- Outdated SynthesisCore banner -->
        <div
          v-if="monitorStore.synthesisOutdated"
          class="bg-error-container text-on-error-container rounded-2xl px-4 py-3 text-xs"
        >
          {{ $t('monitor_page.synthesis_outdated', { version: monitorStore.synthesisVersion }) }}
        </div>

        <!-- ── Session Card ──────────────────────────────────────────────── -->
        <div class="session-card bg-secondary-container rounded-2xl p-4 text-on-secondary-container">
          <p class="text-xs font-semibold uppercase tracking-widest opacity-60 mb-3">
            {{ $t('monitor_page.section.session') }}
          </p>

          <!-- Profile row -->
          <div class="flex items-center gap-3 mb-4">
            <div
              class="w-11 h-11 rounded-full flex items-center justify-center shrink-0 profile-icon-ring"
              :class="profileBgClass"
            >
              <component :is="profileIconComponent" :size="20" class="text-on-primary-container" />
            </div>
            <div>
              <p class="text-base font-semibold leading-tight">{{ profileLabel }}</p>
              <p class="text-xs opacity-60 mt-0.5">{{ $t('home_page.info_card.profile') }}</p>
            </div>
            <div class="ml-auto">
              <span class="profile-badge text-xs px-3 py-1 rounded-full font-semibold" :class="profileBadgeClass">
                {{ profileStatusText }}
              </span>
            </div>
          </div>

          <!-- Profile sparkline -->
          <div class="mb-1">
            <p class="text-xs opacity-50 mb-1 font-medium">{{ $t('monitor_page.profile_history') }}</p>
            <svg width="100%" height="36" class="overflow-visible">
              <defs>
                <linearGradient id="profileGrad" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stop-color="currentColor" stop-opacity="0.3"/>
                  <stop offset="100%" stop-color="currentColor" stop-opacity="0"/>
                </linearGradient>
              </defs>
              <polyline
                v-if="profileSparkPoints"
                :points="profileSparkPoints"
                fill="none"
                stroke="currentColor"
                stroke-width="2"
                stroke-linecap="round"
                stroke-linejoin="round"
                opacity="0.7"
              />
            </svg>
          </div>

          <div class="border-t border-current opacity-10 my-3" />

          <!-- Focused app -->
          <div class="flex items-start gap-3">
            <div class="w-8 h-8 rounded-xl bg-primary-container bg-opacity-40 flex items-center justify-center shrink-0 mt-0.5">
              <AppWindowIcon :size="16" class="text-on-primary-container" />
            </div>
            <div class="min-w-0">
              <p class="text-xs opacity-60 font-medium">{{ $t('monitor_page.focused_app') }}</p>
              <p class="text-sm font-semibold truncate mt-0.5">{{ monitorStore.focusedApp }}</p>
              <p v-if="monitorStore.focusedPid > 0" class="text-xs opacity-40 mt-0.5 font-mono">
                PID {{ monitorStore.focusedPid }} · UID {{ monitorStore.focusedUid }}
              </p>
            </div>
          </div>
        </div>

        <!-- ── Thermal Card ──────────────────────────────────────────────── -->
        <div class="bg-surface-container rounded-2xl p-4 text-on-surface">
          <div class="flex items-center gap-2 mb-3">
            <ThermometerIcon :size="16" class="text-on-surface-variant" />
            <p class="text-sm font-semibold text-primary">
              {{ $t('monitor_page.section.thermal') }}
            </p>
          </div>

          <div v-if="monitorStore.thermalSupported">
            <!-- Big headroom number + ring -->
            <div class="flex items-center gap-4 mb-4">
              <!-- Ring gauge -->
              <div class="relative w-20 h-20 shrink-0">
                <svg width="80" height="80" viewBox="0 0 80 80">
                  <circle cx="40" cy="40" r="32" fill="none" stroke="currentColor" stroke-width="6" opacity="0.12"/>
                  <circle
                    cx="40" cy="40" r="32"
                    fill="none"
                    :stroke="thermalRingColor"
                    stroke-width="6"
                    stroke-linecap="round"
                    stroke-dasharray="201"
                    :stroke-dashoffset="thermalDashOffset"
                    transform="rotate(-90 40 40)"
                    class="thermal-ring-transition"
                  />
                </svg>
                <div class="absolute inset-0 flex flex-col items-center justify-center">
                  <span class="text-lg font-bold leading-none" :class="thermalValueClass">{{ monitorStore.thermalPercent }}</span>
                  <span class="text-xs opacity-50 leading-none mt-0.5">%</span>
                </div>
              </div>

              <div class="flex-1">
                <div class="flex items-center gap-2 mb-1">
                  <span class="text-xs px-2.5 py-0.5 rounded-full font-semibold" :class="thermalBadgeClass">
                    {{ $t(`monitor_page.thermal_label.${monitorStore.thermalLabel}`) }}
                  </span>
                </div>
                <p class="text-sm font-medium text-on-surface-variant">{{ $t('monitor_page.thermal_headroom') }}</p>
                <p class="text-xs text-on-surface-variant opacity-60 mt-1">{{ thermalHintText }}</p>
              </div>
            </div>

            <!-- Thermal sparkline -->
            <div class="bg-surface-container-high rounded-xl p-3">
              <p class="text-xs text-on-surface-variant opacity-60 mb-2 font-medium">{{ $t('monitor_page.thermal_history') }}</p>
              <svg width="100%" height="44" class="overflow-visible">
                <defs>
                  <linearGradient :id="`thermalGrad`" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" :stop-color="thermalSparkColor" stop-opacity="0.4"/>
                    <stop offset="100%" :stop-color="thermalSparkColor" stop-opacity="0"/>
                  </linearGradient>
                </defs>
                <!-- Threshold lines -->
                <line x1="0" :y1="thresholdY(0.20)" x2="100%" :y2="thresholdY(0.20)"
                  stroke="currentColor" stroke-width="0.5" stroke-dasharray="4,4" opacity="0.2"/>
                <line x1="0" :y1="thresholdY(0.35)" x2="100%" :y2="thresholdY(0.35)"
                  stroke="currentColor" stroke-width="0.5" stroke-dasharray="4,4" opacity="0.2"/>
                <!-- Filled area -->
                <polygon v-if="thermalAreaPoints" :points="thermalAreaPoints" :fill="`url(#thermalGrad)`"/>
                <!-- Data line -->
                <polyline
                  v-if="thermalSparkPoints"
                  :points="thermalSparkPoints"
                  fill="none"
                  :stroke="thermalSparkColor"
                  stroke-width="2"
                  stroke-linecap="round"
                  stroke-linejoin="round"
                />
              </svg>
              <div class="flex justify-between text-xs text-on-surface-variant opacity-40 mt-1 font-mono">
                <span>−60s</span><span>now</span>
              </div>
            </div>
          </div>

          <!-- Unsupported: GKI fallback notice -->
          <div v-else class="bg-surface-container-high rounded-xl p-4">
            <div class="flex items-center gap-3 mb-2">
              <div class="w-9 h-9 rounded-full bg-tertiary-container flex items-center justify-center">
                <ThermometerIcon :size="18" class="text-on-tertiary-container" />
              </div>
              <div>
                <p class="text-sm font-semibold text-on-surface">{{ $t('monitor_page.thermal_unsupported_title') }}</p>
                <p class="text-xs text-on-surface-variant mt-0.5">API 31+ required</p>
              </div>
            </div>
            <p class="text-xs text-on-surface-variant leading-relaxed">
              {{ $t('monitor_page.thermal_unsupported') }}
            </p>
            <div class="mt-3 flex items-center gap-1.5">
              <div class="w-1.5 h-1.5 rounded-full bg-tertiary"></div>
              <p class="text-xs text-on-surface-variant opacity-70">{{ $t('monitor_page.thermal_gki_note') }}</p>
            </div>
          </div>
        </div>

        <!-- ── Status Grid ──────────────────────────────────────────────── -->
        <div class="grid grid-cols-2 gap-3">

          <!-- Charging -->
          <div
            class="status-chip rounded-2xl p-4 flex items-center gap-3 transition-all duration-300"
            :class="monitorStore.charging ? 'bg-tertiary-container text-on-tertiary-container chip-active' : 'bg-surface-container text-on-surface'"
          >
            <div class="status-icon-wrap w-9 h-9 rounded-xl flex items-center justify-center shrink-0"
              :class="monitorStore.charging ? 'bg-tertiary bg-opacity-20' : 'bg-surface-container-high'">
              <BoltChargeIcon v-if="monitorStore.charging" :size="18" />
              <BatteryFullIcon v-else :size="18" />
            </div>
            <div>
              <p class="text-xs opacity-60 font-medium">{{ $t('monitor_page.charging') }}</p>
              <p class="text-sm font-semibold mt-0.5">
                {{ monitorStore.charging ? $t('common.enabled') : $t('common.disabled') }}
              </p>
            </div>
          </div>

          <!-- Audio -->
          <div
            class="status-chip rounded-2xl p-4 flex items-center gap-3 transition-all duration-300"
            :class="monitorStore.audioActive ? 'bg-primary-container text-on-primary-container chip-active' : 'bg-surface-container text-on-surface'"
          >
            <div class="status-icon-wrap w-9 h-9 rounded-xl flex items-center justify-center shrink-0"
              :class="monitorStore.audioActive ? 'bg-primary bg-opacity-20' : 'bg-surface-container-high'">
              <VolumeUpIcon v-if="monitorStore.audioActive" :size="18" />
              <VolumeOffIcon v-else :size="18" />
            </div>
            <div>
              <p class="text-xs opacity-60 font-medium">{{ $t('monitor_page.audio') }}</p>
              <p class="text-sm font-semibold mt-0.5">
                {{ monitorStore.audioActive ? $t('monitor_page.audio_active') : $t('monitor_page.audio_silent') }}
              </p>
            </div>
          </div>

          <!-- Screen -->
          <div
            class="status-chip rounded-2xl p-4 flex items-center gap-3 transition-all duration-300"
            :class="monitorStore.screenAwake ? 'bg-surface-container-high text-on-surface chip-active' : 'bg-surface-container text-on-surface'"
          >
            <div class="status-icon-wrap w-9 h-9 rounded-xl flex items-center justify-center shrink-0"
              :class="monitorStore.screenAwake ? 'bg-primary bg-opacity-15' : 'bg-surface-container-high'">
              <SmartphoneIcon v-if="monitorStore.screenAwake" :size="18" />
              <SmartphoneOffIcon v-else :size="18" />
            </div>
            <div>
              <p class="text-xs opacity-60 font-medium">{{ $t('monitor_page.screen') }}</p>
              <p class="text-sm font-semibold mt-0.5">
                {{ monitorStore.screenAwake ? $t('monitor_page.screen_on') : $t('monitor_page.screen_off') }}
              </p>
            </div>
          </div>

          <!-- DND -->
          <div
            class="status-chip rounded-2xl p-4 flex items-center gap-3 transition-all duration-300"
            :class="monitorStore.zenMode > 0 ? 'bg-surface-container-high text-on-surface chip-active' : 'bg-surface-container text-on-surface'"
          >
            <div class="status-icon-wrap w-9 h-9 rounded-xl flex items-center justify-center shrink-0"
              :class="monitorStore.zenMode > 0 ? 'bg-tertiary bg-opacity-15' : 'bg-surface-container-high'">
              <NotificationsOffIcon v-if="monitorStore.zenMode > 0" :size="18" />
              <NotificationsActiveIcon v-else :size="18" />
            </div>
            <div>
              <p class="text-xs opacity-60 font-medium">{{ $t('monitor_page.dnd') }}</p>
              <p class="text-sm font-semibold mt-0.5">{{ zenModeLabel }}</p>
            </div>
          </div>
        </div>

        <!-- ── Battery Saver full-width ──────────────────────────────────── -->
        <div
          class="status-chip rounded-2xl p-4 flex items-center gap-3 transition-all duration-300"
          :class="monitorStore.batterySaver ? 'bg-error-container text-on-error-container chip-active' : 'bg-surface-container text-on-surface'"
        >
          <div class="status-icon-wrap w-9 h-9 rounded-xl flex items-center justify-center shrink-0"
            :class="monitorStore.batterySaver ? 'bg-error bg-opacity-20' : 'bg-surface-container-high'">
            <BatterySaverIcon :size="18" />
          </div>
          <div>
            <p class="text-xs opacity-60 font-medium">{{ $t('monitor_page.battery_saver') }}</p>
            <p class="text-sm font-semibold mt-0.5">
              {{ monitorStore.batterySaver ? $t('monitor_page.battery_saver_on') : $t('monitor_page.battery_saver_off') }}
            </p>
          </div>
          <div class="ml-auto">
            <div class="w-2 h-2 rounded-full" :class="monitorStore.batterySaver ? 'bg-error animate-pulse' : 'bg-primary'"></div>
          </div>
        </div>

      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted } from 'vue'
import { useMonitorStore } from '@/stores/Monitor'
import { useI18n } from 'vue-i18n'

// Icons
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import BatteryFullIcon from '@/components/icons/BatteryFull.vue'
import VolumeUpIcon from '@/components/icons/VolumeUp.vue'
import VolumeOffIcon from '@/components/icons/VolumeOff.vue'
import SmartphoneIcon from '@/components/icons/Smartphone.vue'
import SmartphoneOffIcon from '@/components/icons/SmartphoneOff.vue'
import NotificationsOffIcon from '@/components/icons/NotificationsOff.vue'
import NotificationsActiveIcon from '@/components/icons/NotificationsActive.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import ThermometerIcon from '@/components/icons/Thermostat.vue'
import AppWindowIcon from '@/components/icons/AppWindow.vue'

// Profile icons (reuse existing icon components)
import RocketIcon from '@/components/icons/Star.vue'
import FeatherIcon from '@/components/icons/Feather.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'

const { t } = useI18n()
const monitorStore = useMonitorStore()

onMounted(() => monitorStore.init())
onUnmounted(() => monitorStore.stopPolling())

// ── Live dot ─────────────────────────────────────────────────────────────────

const dotClass = computed(() =>
  monitorStore.lastError
    ? 'bg-error animate-pulse'
    : 'bg-primary animate-pulse'
)

// ── Profile ──────────────────────────────────────────────────────────────────

const profileIconMap = {
  performance:      RocketIcon,
  performance_lite: FeatherIcon,
  balanced:         ChipsetIcon,
  powersave:        BatterySaverIcon,
  initializing:     ChipsetIcon,
  unknown:          ChipsetIcon,
}

const profileBgMap = {
  performance:      'bg-primary-container',
  performance_lite: 'bg-tertiary-container',
  balanced:         'bg-secondary-container',
  powersave:        'bg-surface-container-high',
  initializing:     'bg-surface-container',
  unknown:          'bg-surface-container',
}

const profileBadgeMap = {
  performance:      'bg-primary text-on-primary',
  performance_lite: 'bg-tertiary text-on-tertiary',
  balanced:         'bg-secondary text-on-secondary',
  powersave:        'bg-surface-container-highest text-on-surface',
  initializing:     'bg-surface-container text-on-surface-variant',
  unknown:          'bg-surface-container text-on-surface-variant',
}

const profileIconComponent = computed(() => profileIconMap[monitorStore.currentProfile] ?? ChipsetIcon)
const profileBgClass = computed(() => profileBgMap[monitorStore.currentProfile] ?? 'bg-surface-container')
const profileBadgeClass = computed(() => profileBadgeMap[monitorStore.currentProfile] ?? 'bg-surface-container text-on-surface-variant')

const profileLabel = computed(() => {
  const key = monitorStore.currentProfile
  const translation = t(`profiles.${key}`)
  return translation !== `profiles.${key}` ? translation : key
})

const profileStatusText = computed(() => {
  const key = monitorStore.currentProfile
  if (key === 'initializing') return t('common.loading')
  if (key === 'unknown') return t('common.unknown')
  return t('monitor_page.profile_active')
})

// Profile sparkline
const SPARK_H = 36
const SPARK_PROFILE_MAX = 4

const profileSparkPoints = computed(() => {
  const pts = monitorStore.profileHistory
  if (pts.length < 2) return ''
  const n = pts.length
  return pts.map((p, i) => {
    const x = (i / (n - 1)) * 100
    const y = SPARK_H - (p.v / SPARK_PROFILE_MAX) * SPARK_H
    return `${x}%,${y}`
  }).join(' ')
})

// ── Thermal ──────────────────────────────────────────────────────────────────

const thermalValueClass = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return 'text-error'
  if (l === 'warm') return 'text-tertiary'
  return 'text-primary'
})

const thermalBadgeClass = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return 'bg-error text-on-error'
  if (l === 'warm') return 'bg-tertiary text-on-tertiary'
  return 'bg-primary text-on-primary'
})

const thermalHintText = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return t('monitor_page.thermal_hint.hot')
  if (l === 'warm') return t('monitor_page.thermal_hint.warm')
  return t('monitor_page.thermal_hint.cool')
})

const thermalRingColor = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return 'var(--color-error, #f2b8b5)'
  if (l === 'warm') return 'var(--color-tertiary, #f0bc95)'
  return 'var(--color-primary, #ffb0cc)'
})

// Ring circumference = 2π×32 ≈ 201
const thermalDashOffset = computed(() => {
  const pct = monitorStore.thermalPercent ?? 0
  return 201 - (pct / 100) * 201
})

const thermalSparkColor = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return 'var(--color-error, #f2b8b5)'
  if (l === 'warm') return 'var(--color-tertiary, #f0bc95)'
  return 'var(--color-primary, #ffb0cc)'
})

const SPARK_THERMAL_H = 44

function thresholdY(v) {
  return SPARK_THERMAL_H - v * SPARK_THERMAL_H
}

const thermalSparkPoints = computed(() => {
  const pts = monitorStore.thermalHistory.filter(p => p.v >= 0)
  if (pts.length < 2) return ''
  const n = pts.length
  return pts.map((p, i) => {
    const x = (i / (n - 1)) * 100
    const y = SPARK_THERMAL_H - p.v * SPARK_THERMAL_H
    return `${x}%,${y}`
  }).join(' ')
})

const thermalAreaPoints = computed(() => {
  const pts = monitorStore.thermalHistory.filter(p => p.v >= 0)
  if (pts.length < 2) return ''
  const n = pts.length
  const linePoints = pts.map((p, i) => {
    const x = (i / (n - 1)) * 100
    const y = SPARK_THERMAL_H - p.v * SPARK_THERMAL_H
    return `${x}%,${y}`
  })
  const first = `0%,${SPARK_THERMAL_H}`
  const last = `100%,${SPARK_THERMAL_H}`
  return `${first} ${linePoints.join(' ')} ${last}`
})

// ── Zen mode label ────────────────────────────────────────────────────────────

const zenModeLabel = computed(() => {
  switch (monitorStore.zenMode) {
    case 1: return t('monitor_page.dnd_priority')
    case 2: return t('monitor_page.dnd_silence')
    case 3: return t('monitor_page.dnd_alarms')
    default: return t('common.disabled')
  }
})
</script>

<style scoped>
.live-dot {
  box-shadow: 0 0 0 3px color-mix(in srgb, var(--color-primary) 20%, transparent);
}

.thermal-ring-transition {
  transition: stroke-dashoffset 0.8s cubic-bezier(0.4, 0, 0.2, 1), stroke 0.5s ease;
}

.status-chip {
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}

.chip-active {
  box-shadow: 0 2px 12px color-mix(in srgb, currentColor 8%, transparent);
}

.profile-icon-ring {
  box-shadow: 0 0 0 3px color-mix(in srgb, currentColor 15%, transparent);
}

.session-card {
  background: linear-gradient(135deg, var(--color-secondary-container) 0%, var(--color-secondary-container) 100%);
}

.status-icon-wrap {
  transition: all 0.3s ease;
}
</style>
