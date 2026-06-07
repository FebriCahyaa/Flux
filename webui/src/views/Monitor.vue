<template>
  <div class="page monitor-page h-full flex flex-col">

    <!-- Header -->
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-3xl mx-auto p-5 pb-3">
        <div class="flex justify-between items-center text-on-surface">
          <h1 class="text-xl font-semibold">{{ $t('monitor_page.title') }}</h1>
          <div class="flex items-center gap-2">
            <span class="text-xs text-on-surface-variant">{{ $t('monitor_page.live') }}</span>
            <span class="w-2 h-2 rounded-full" :class="dotClass"></span>
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
          class="bg-error-container text-on-error-container rounded-xl px-4 py-3 text-xs"
        >
          {{ monitorStore.lastError }}
        </div>

        <!-- ── Profile & Game ────────────────────────────────────────────── -->
        <div class="bg-secondary-container rounded-xl p-4 text-on-secondary-container">
          <p class="text-xs font-medium uppercase tracking-wide opacity-70 mb-3">
            {{ $t('monitor_page.section.session') }}
          </p>

          <!-- Active profile chip -->
          <div class="flex items-center gap-3 mb-3">
            <div
              class="w-10 h-10 rounded-full flex items-center justify-center shrink-0"
              :class="profileBgClass"
            >
              <span class="text-lg">{{ profileEmoji }}</span>
            </div>
            <div>
              <p class="text-sm font-semibold">{{ profileLabel }}</p>
              <p class="text-xs opacity-70">{{ $t('home_page.info_card.profile') }}</p>
            </div>
          </div>

          <!-- Profile history sparkline -->
          <div class="mb-1">
            <p class="text-xs opacity-60 mb-1">{{ $t('monitor_page.profile_history') }}</p>
            <svg width="100%" height="32" class="overflow-visible">
              <polyline
                :points="profileSparkPoints"
                fill="none"
                stroke="currentColor"
                stroke-width="1.5"
                stroke-linecap="round"
                stroke-linejoin="round"
                opacity="0.6"
              />
            </svg>
          </div>

          <div class="border-t border-current opacity-10 my-3" />

          <!-- Focused app -->
          <div class="flex items-start gap-3">
            <span class="text-base mt-0.5">🎮</span>
            <div class="min-w-0">
              <p class="text-xs opacity-70">{{ $t('monitor_page.focused_app') }}</p>
              <p class="text-sm font-medium truncate">{{ monitorStore.focusedApp }}</p>
              <p v-if="monitorStore.focusedPid > 0" class="text-xs opacity-50 mt-0.5">
                PID {{ monitorStore.focusedPid }} · UID {{ monitorStore.focusedUid }}
              </p>
            </div>
          </div>
        </div>

        <!-- ── Thermal ───────────────────────────────────────────────────── -->
        <div class="bg-surface-container rounded-xl p-4 text-on-surface">
          <p class="text-xs font-medium uppercase tracking-wide text-on-surface-variant mb-3">
            {{ $t('monitor_page.section.thermal') }}
          </p>

          <div v-if="monitorStore.thermalSupported">
            <!-- Big headroom number -->
            <div class="flex items-end gap-2 mb-2">
              <span class="text-4xl font-bold" :class="thermalValueClass">
                {{ monitorStore.thermalPercent }}%
              </span>
              <span class="text-sm text-on-surface-variant mb-1">
                {{ $t('monitor_page.thermal_headroom') }}
              </span>
            </div>

            <!-- Progress bar -->
            <div class="w-full h-2 bg-surface-container-highest rounded-full overflow-hidden mb-3">
              <div
                class="h-full rounded-full transition-all duration-500"
                :class="thermalBarClass"
                :style="{ width: `${monitorStore.thermalPercent}%` }"
              />
            </div>

            <!-- Tier badge -->
            <div class="flex items-center gap-2 mb-3">
              <span
                class="text-xs px-2 py-0.5 rounded-full font-medium"
                :class="thermalBadgeClass"
              >
                {{ $t(`monitor_page.thermal_label.${monitorStore.thermalLabel}`) }}
              </span>
              <span class="text-xs text-on-surface-variant">
                {{ thermalHintText }}
              </span>
            </div>

            <!-- Thermal sparkline -->
            <p class="text-xs text-on-surface-variant mb-1">{{ $t('monitor_page.thermal_history') }}</p>
            <svg width="100%" height="40" class="overflow-visible">
              <!-- Threshold lines -->
              <line
                x1="0" :y1="thresholdY(0.20)" x2="100%" :y2="thresholdY(0.20)"
                stroke="currentColor" stroke-width="0.5" stroke-dasharray="3,3" opacity="0.25"
              />
              <line
                x1="0" :y1="thresholdY(0.35)" x2="100%" :y2="thresholdY(0.35)"
                stroke="currentColor" stroke-width="0.5" stroke-dasharray="3,3" opacity="0.25"
              />
              <!-- Data line -->
              <polyline
                :points="thermalSparkPoints"
                fill="none"
                :stroke="thermalSparkColor"
                stroke-width="2"
                stroke-linecap="round"
                stroke-linejoin="round"
              />
            </svg>
            <div class="flex justify-between text-xs text-on-surface-variant opacity-40 mt-1">
              <span>−60s</span><span>now</span>
            </div>
          </div>

          <div v-else class="text-sm text-on-surface-variant italic">
            {{ $t('monitor_page.thermal_unsupported') }}
          </div>
        </div>

        <!-- ── Status chips ──────────────────────────────────────────────── -->
        <div class="grid grid-cols-2 gap-3">

          <!-- Charging -->
          <div
            class="rounded-xl p-4 flex items-center gap-3 transition-colors"
            :class="monitorStore.charging
              ? 'bg-tertiary-container text-on-tertiary-container'
              : 'bg-surface-container text-on-surface'"
          >
            <span class="text-2xl">{{ monitorStore.charging ? '⚡' : '🔋' }}</span>
            <div>
              <p class="text-xs opacity-70">{{ $t('monitor_page.charging') }}</p>
              <p class="text-sm font-semibold">
                {{ monitorStore.charging
                    ? $t('common.enabled')
                    : $t('common.disabled') }}
              </p>
            </div>
          </div>

          <!-- Audio -->
          <div
            class="rounded-xl p-4 flex items-center gap-3 transition-colors"
            :class="monitorStore.audioActive
              ? 'bg-primary-container text-on-primary-container'
              : 'bg-surface-container text-on-surface'"
          >
            <span class="text-2xl">{{ monitorStore.audioActive ? '🔊' : '🔇' }}</span>
            <div>
              <p class="text-xs opacity-70">{{ $t('monitor_page.audio') }}</p>
              <p class="text-sm font-semibold">
                {{ monitorStore.audioActive
                    ? $t('monitor_page.audio_active')
                    : $t('monitor_page.audio_silent') }}
              </p>
            </div>
          </div>

          <!-- Screen -->
          <div
            class="rounded-xl p-4 flex items-center gap-3 transition-colors"
            :class="monitorStore.screenAwake
              ? 'bg-surface-container-high text-on-surface'
              : 'bg-surface-container text-on-surface'"
          >
            <span class="text-2xl">{{ monitorStore.screenAwake ? '📱' : '🌑' }}</span>
            <div>
              <p class="text-xs opacity-70">{{ $t('monitor_page.screen') }}</p>
              <p class="text-sm font-semibold">
                {{ monitorStore.screenAwake
                    ? $t('monitor_page.screen_on')
                    : $t('monitor_page.screen_off') }}
              </p>
            </div>
          </div>

          <!-- DND / Zen -->
          <div
            class="rounded-xl p-4 flex items-center gap-3 transition-colors"
            :class="monitorStore.zenMode > 0
              ? 'bg-surface-container-high text-on-surface'
              : 'bg-surface-container text-on-surface'"
          >
            <span class="text-2xl">{{ monitorStore.zenMode > 0 ? '🔕' : '🔔' }}</span>
            <div>
              <p class="text-xs opacity-70">{{ $t('monitor_page.dnd') }}</p>
              <p class="text-sm font-semibold">
                {{ zenModeLabel }}
              </p>
            </div>
          </div>
        </div>

        <!-- ── Battery saver full-width ──────────────────────────────────── -->
        <div
          class="rounded-xl p-4 flex items-center gap-3 transition-colors"
          :class="monitorStore.batterySaver
            ? 'bg-error-container text-on-error-container'
            : 'bg-surface-container text-on-surface'"
        >
          <span class="text-2xl">{{ monitorStore.batterySaver ? '🪫' : '🟢' }}</span>
          <div>
            <p class="text-xs opacity-70">{{ $t('monitor_page.battery_saver') }}</p>
            <p class="text-sm font-semibold">
              {{ monitorStore.batterySaver
                  ? $t('monitor_page.battery_saver_on')
                  : $t('monitor_page.battery_saver_off') }}
            </p>
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

const { t } = useI18n()
const monitorStore = useMonitorStore()

// ── Lifecycle ────────────────────────────────────────────────────────────────

onMounted(() => monitorStore.init())
onUnmounted(() => monitorStore.stopPolling())

// ── Live dot ─────────────────────────────────────────────────────────────────

const dotClass = computed(() =>
  monitorStore.lastError
    ? 'bg-error animate-pulse'
    : 'bg-primary animate-pulse'
)

// ── Profile ──────────────────────────────────────────────────────────────────

const profileEmojiMap = {
  performance:      '🚀',
  performance_lite: '🪶',
  balanced:         '⚖️',
  powersave:        '🌿',
  initializing:     '⏳',
  unknown:          '❓',
}

const profileBgMap = {
  performance:      'bg-primary-container',
  performance_lite: 'bg-tertiary-container',
  balanced:         'bg-secondary-container',
  powersave:        'bg-surface-container-high',
  initializing:     'bg-surface-container',
  unknown:          'bg-surface-container',
}

const profileEmoji = computed(() => profileEmojiMap[monitorStore.currentProfile] ?? '❓')
const profileBgClass = computed(() => profileBgMap[monitorStore.currentProfile] ?? 'bg-surface-container')
const profileLabel = computed(() => {
  const key = monitorStore.currentProfile
  const translation = t(`profiles.${key}`)
  return translation !== `profiles.${key}` ? translation : key
})

// Profile sparkline: map enum 0–4 to Y (inverted — 0 = bottom, 4 = top visual)
const SPARK_H = 32
const SPARK_PROFILE_MAX = 4

const profileSparkPoints = computed(() => {
  const pts = monitorStore.profileHistory
  if (pts.length < 2) return ''
  const w = 100
  const n = pts.length
  return pts.map((p, i) => {
    const x = (i / (n - 1)) * w
    // performance=1 at top, powersave=4 at bottom
    const y = SPARK_H - (p.v / SPARK_PROFILE_MAX) * SPARK_H
    return `${x},${y}`
  }).join(' ')
})

// ── Thermal ──────────────────────────────────────────────────────────────────

const thermalValueClass = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return 'text-error'
  if (l === 'warm') return 'text-tertiary'
  return 'text-primary'
})

const thermalBarClass = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return 'bg-error'
  if (l === 'warm') return 'bg-tertiary'
  return 'bg-primary'
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

const thermalSparkColor = computed(() => {
  const l = monitorStore.thermalLabel
  if (l === 'hot')  return 'var(--color-error, #f2b8b5)'
  if (l === 'warm') return 'var(--color-tertiary, #f0bc95)'
  return 'var(--color-primary, #ffb0cc)'
})

// Thermal sparkline: 0.0=bottom, 1.0=top in 40px height
const SPARK_THERMAL_H = 40

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
