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

        <!-- ── Live game session (SessionRecorder) ──────────────────────── -->
        <section v-if="sessions.liveActive" class="live-card m3-enter rounded-[32px] p-5 bg-primary-container text-on-primary-container relative overflow-hidden">
          <span class="live-deco shape-burst bg-primary" aria-hidden="true"></span>

          <div class="relative flex items-center gap-3">
            <span class="app-icon shape-cookie9 bg-surface-container-high">
              <img v-if="sessions.icons[live.package]" :src="sessions.icons[live.package]" alt="" @error="sessions.icons[live.package] = ''" />
              <span v-else class="app-letter">{{ appName(live.package).charAt(0) }}</span>
            </span>
            <div class="min-w-0 flex-1">
              <p class="text-base font-semibold truncate">{{ appName(live.package) }}</p>
              <p class="text-xs opacity-80 flex items-center gap-1.5">
                <span class="rec-dot"></span>{{ $t('sessions.recording') }} · {{ formatDuration(live.elapsed) }}
              </p>
            </div>
            <span class="shrink-0 rounded-full px-3 py-1 text-xs font-semibold"
              :class="live.lite ? 'bg-tertiary text-on-tertiary' : 'bg-primary text-on-primary'">
              {{ live.lite ? $t('profiles.performance_lite') : $t('profiles.performance') }}
            </span>
          </div>

          <!-- Big FPS -->
          <div class="relative flex items-end gap-3 mt-5">
            <span class="m3-headline fps-big tabular-nums">{{ fmt(live.now.fps) }}</span>
            <div class="pb-2">
              <p class="text-sm font-semibold">FPS</p>
              <p class="text-[11px] opacity-70">{{ fpsSourceLabel(live.fps_source) }}</p>
            </div>
          </div>
          <LineChart
            v-if="liveFps.some((v) => v !== null)"
            class="relative mt-2"
            :height="90"
            :min="0"
            :series="[{ values: liveFps, color: 'var(--color-primary)', area: true, width: 2.5 }]"
            :reference="dropLine"
            :label="$t('sessions.fps_chart')"
          />
          <p v-else class="relative text-xs opacity-80 mt-2">{{ $t('sessions.fps_unavailable') }}</p>

          <!-- Stats -->
          <div class="relative grid grid-cols-4 gap-1 mt-4">
            <div v-for="stat in liveStats" :key="stat.key" class="stat-tile">
              <p class="text-[11px] opacity-75">{{ stat.label }}</p>
              <p class="text-lg font-bold tabular-nums">{{ stat.value }}</p>
            </div>
          </div>

          <!-- Temperatures -->
          <div class="relative grid grid-cols-2 gap-2 mt-2">
            <div class="temp-tile">
              <ThermometerIcon :size="18" />
              <div>
                <p class="text-[11px] opacity-75">CPU</p>
                <p class="text-xl font-bold tabular-nums leading-tight">{{ fmt(live.now.cpu, 1) }}°</p>
                <p class="text-[11px] opacity-75 tabular-nums">{{ $t('sessions.max') }} {{ fmt(live.summary.cpu_max, 1) }}°</p>
              </div>
            </div>
            <div class="temp-tile">
              <BatteryFullIcon :size="18" />
              <div>
                <p class="text-[11px] opacity-75">{{ $t('sessions.battery') }}</p>
                <p class="text-xl font-bold tabular-nums leading-tight">{{ fmt(live.now.battery, 1) }}°</p>
                <p class="text-[11px] opacity-75 tabular-nums">{{ $t('sessions.max') }} {{ fmt(live.summary.battery_max, 1) }}°</p>
              </div>
            </div>
          </div>
        </section>

        <!-- ── Session Card (no game running) ────────────────────────────── -->
        <div v-else class="session-card bg-secondary-container rounded-2xl p-4 text-on-secondary-container">
          <p class="text-xs font-semibold uppercase tracking-widest opacity-60 mb-3">
            {{ $t('monitor_page.section.session') }}
          </p>

          <!-- Profile row -->
          <div class="flex items-center gap-3">
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

            <!-- Thermal headroom over the last minute (shown once there are samples) -->
            <div v-if="thermalChart.length > 1" class="bg-surface-container-high rounded-xl px-3 pt-2 pb-1">
              <p class="text-xs text-on-surface-variant opacity-60 font-medium">
                {{ $t('monitor_page.thermal_history') }}
              </p>
              <LineChart
                :series="[{ values: thermalChart, color: thermalSparkColor, area: true }]"
                :height="72"
                :min="0"
                :max="100"
                unit="%"
                :label="$t('monitor_page.thermal_history')"
              />
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


        <!-- ── Game session history ──────────────────────────────────────── -->
        <div class="flex items-center justify-between px-4 pt-4 pb-1">
          <h2 class="text-sm font-semibold text-primary">{{ $t('sessions.history_title') }}</h2>
          <span v-if="sessions.history.length" class="text-xs text-on-surface-variant">{{ sessions.history.length }}</span>
        </div>
        <div v-if="!sessions.history.length" class="m3-card p-5 flex items-center gap-4">
          <span class="empty-badge shape-clover4 bg-secondary-container text-on-secondary-container"><GamesIcon /></span>
          <p class="text-sm text-on-surface-variant">{{ $t('sessions.empty') }}</p>
        </div>
        <div v-else class="pb-2">
          <div v-for="s in sessions.history" :key="s.id" class="md3-list">
            <RippleComponent class="md3-list-item" tabindex="0" @click="openSession(s)">
              <div class="flex items-center gap-3 px-4 py-3.5">
                <span class="app-icon shape-cookie9 bg-surface-container-high">
                  <img v-if="sessions.icons[s.package]" :src="sessions.icons[s.package]" alt="" @error="sessions.icons[s.package] = ''" />
                  <span v-else class="app-letter">{{ appName(s.package).charAt(0) }}</span>
                </span>
                <div class="min-w-0 flex-1">
                  <p class="text-sm font-semibold text-on-surface truncate">{{ appName(s.package) }}</p>
                  <p class="text-xs text-on-surface-variant">{{ formatDate(s.start) }} · {{ formatDuration(s.duration) }}</p>
                  <div class="flex flex-wrap gap-1 mt-1.5">
                    <span class="chip" :class="dropTone(s.summary.drops)">
                      {{ $t('sessions.drops_count', s.summary.drops) }}
                    </span>
                    <span v-if="s.summary.cpu_max !== null" class="chip bg-surface-container-highest" :class="tempTone(s.summary.cpu_max)">
                      CPU {{ fmt(s.summary.cpu_max) }}°
                    </span>
                  </div>
                </div>
                <div class="text-right shrink-0">
                  <p class="m3-headline text-2xl text-on-surface tabular-nums">{{ fmt(s.summary.fps_avg) }}</p>
                  <p class="text-[11px] text-on-surface-variant">{{ $t('sessions.avg_fps') }}</p>
                </div>
              </div>
            </RippleComponent>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { useMonitorStore } from '@/stores/Monitor'
import { useSessionsStore, formatDuration, fmt, tempTone } from '@/stores/Sessions'
import { useI18n } from 'vue-i18n'
import LineChart from '@/components/ui/LineChart.vue'
import RippleComponent from '@/components/ui/Ripple.vue'
import GamesIcon from '@/components/icons/Games.vue'

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

const sessions = useSessionsStore()
const router = useRouter()

onMounted(() => {
  monitorStore.init()
  sessions.loadHistory()
})

// ── Game sessions ────────────────────────────────────────────────────────────

const live = computed(() => sessions.live)
const liveFps = computed(() => (live.value?.recent ?? []).map((r) => r[0]))
const dropLine = computed(() => {
  const m = live.value?.summary?.fps_median
  return m ? m * 0.8 : null
})
const liveStats = computed(() => {
  const s = live.value?.summary ?? {}
  return [
    { key: 'avg', label: t('sessions.avg'), value: fmt(s.fps_avg) },
    { key: 'low', label: t('sessions.low1'), value: fmt(s.fps_low1) },
    { key: 'drops', label: t('sessions.drops'), value: s.drops ?? 0 },
    { key: 'stable', label: t('sessions.stability'), value: s.stability === null || s.stability === undefined ? '–' : `${fmt(s.stability)}%` },
  ]
})

const appName = (pkg) => sessions.labels[pkg] || pkg
const fpsSourceLabel = (src) => (src ? t(`sessions.source.${src}`) : t('sessions.source.none'))
const formatDate = (ms) =>
  new Date(ms).toLocaleString([], { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' })
const openSession = (s) => router.push(`/monitor/session/${s.id}`)
const dropTone = (n) =>
  !n ? 'bg-secondary-container text-on-secondary-container'
    : n <= 5 ? 'bg-tertiary-container text-on-tertiary-container'
      : 'bg-error-container text-on-error-container'
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

// Headroom samples of the last minute, in percent (unsupported samples dropped)
const thermalChart = computed(() =>
  monitorStore.thermalHistory.filter(p => p.v >= 0).map(p => Math.round(p.v * 100)),
)

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
.live-deco {
  position: absolute;
  width: 200px;
  height: 200px;
  right: -60px;
  top: -70px;
  opacity: 0.14;
  animation: live-spin 24s linear infinite;
}

.fps-big {
  font-size: 72px;
  line-height: 0.9;
}

.rec-dot {
  width: 7px;
  height: 7px;
  border-radius: 999px;
  background: var(--color-error);
  animation: rec-blink 1.2s ease-in-out infinite;
}

.stat-tile {
  padding: 10px 10px;
  border-radius: 6px;
  background: color-mix(in srgb, var(--color-on-primary-container) 8%, transparent);
}

.stat-tile:first-child {
  border-radius: 18px 6px 6px 18px;
}

.stat-tile:last-child {
  border-radius: 6px 18px 18px 6px;
}

.temp-tile {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border-radius: 18px;
  background: color-mix(in srgb, var(--color-on-primary-container) 8%, transparent);
}

.app-icon {
  width: 44px;
  height: 44px;
  flex-shrink: 0;
  display: grid;
  place-items: center;
  overflow: hidden;
}

.app-icon img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.empty-badge {
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.app-letter {
  font-size: 18px;
  font-weight: 700;
  color: var(--color-on-surface-variant);
}

.chip {
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 999px;
}

@keyframes live-spin {
  to {
    transform: rotate(1turn);
  }
}

@keyframes rec-blink {
  50% {
    opacity: 0.3;
  }
}

@media (prefers-reduced-motion: reduce) {
  .live-deco,
  .rec-dot {
    animation: none;
  }
}

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
