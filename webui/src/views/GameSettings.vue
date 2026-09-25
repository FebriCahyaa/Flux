<template>
  <div class="page game-settings-page h-full flex flex-col overflow-hidden bg-surface">
    <div class="max-w-3xl mx-auto h-full flex flex-col w-full">
      <div class="flex-none p-5 pb-3">
        <button
          @click="router.back()"
          class="m3-press w-10 h-10 -ms-2 rounded-full grid place-items-center text-on-surface hover:bg-surface-container-high"
          :aria-label="$t('common.cancel')"
        >
          <ArrowLeftIcon class="w-6 h-6 rtl:rotate-180" />
        </button>
      </div>

      <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll px-4">
        <!-- App header -->
        <div class="flex flex-col items-center text-center mt-2 mb-5">
          <span class="hero-icon shape-cookie12 bg-surface-container-high">
            <img :src="currentApp.icon" @error="iconError" :alt="currentApp.appName" />
          </span>
          <h1 class="m3-headline text-3xl text-on-surface mt-4 px-4 break-words">
            {{ currentApp.appName || currentApp.packageName }}
          </h1>
          <p class="allow-copy text-xs text-on-surface-variant mt-1">
            {{ currentApp.packageName }}
          </p>
          <div class="flex gap-2 mt-4">
            <button class="m3-press m3-press-morph pill bg-primary text-on-primary" @click="launch">
              <OpenInNew :size="18" />{{ $t('game_settings.launch_app') }}
            </button>
            <button
              class="m3-press m3-press-morph pill bg-secondary-container text-on-secondary-container"
              @click="appInfo"
            >
              <InformationOutline :size="18" />{{ $t('game_settings.app_info') }}
            </button>
          </div>
        </div>

        <!-- Main switch -->
        <div
          class="main-card m3-enter"
          :class="
            settings.isEnabled
              ? 'bg-primary-container text-on-primary-container'
              : 'bg-surface-container-high text-on-surface'
          "
        >
          <span
            class="badge shape-burst"
            :class="
              settings.isEnabled ? 'bg-primary text-on-primary' : 'bg-surface-container-highest'
            "
          >
            <Candy />
          </span>
          <div class="flex-1 min-w-0">
            <h2 class="text-base font-semibold">{{ $t('game_settings.enable_tweaks') }}</h2>
            <p class="text-xs opacity-80 mt-0.5">
              {{
                settings.isEnabled
                  ? $t('game_settings.enabled_hint')
                  : $t('game_settings.disabled_hint')
              }}
            </p>
          </div>
          <ToggleSwitch :model-value="settings.isEnabled" @update:model-value="setEnabled" />
        </div>

        <!-- Preferences -->
        <h2
          class="text-sm font-semibold text-primary px-4 pt-5 pb-2"
          :class="{ 'opacity-50': !settings.isEnabled }"
        >
          {{ $t('game_settings.preferences') }}
        </h2>
        <div :class="{ 'opacity-50 pointer-events-none': !settings.isEnabled }">
          <div class="md3-list">
            <div class="md3-list-item flex items-center gap-4 px-5 py-4 cursor-default">
              <span class="badge-sm shape-clover4 bg-tertiary-container text-on-tertiary-container"
                ><Feather
              /></span>
              <div class="flex-1 min-w-0">
                <h3 class="text-sm font-semibold text-on-surface">
                  {{ $t('game_settings.lite_mode') }}
                </h3>
                <p class="text-xs text-on-surface-variant mt-0.5">
                  {{
                    globalLite
                      ? $t('game_settings.lite_forced')
                      : $t('game_settings.lite_mode_description')
                  }}
                </p>
              </div>
              <ToggleSwitch
                :model-value="settings.isEnabled && (globalLite || settings.lite_mode)"
                :disabled="!settings.isEnabled || globalLite"
                @update:model-value="(v) => setOption('lite_mode', v)"
              />
            </div>
          </div>
          <div class="md3-list">
            <div class="md3-list-item flex items-center gap-4 px-5 py-4 cursor-default">
              <span
                class="badge-sm shape-pentagon bg-secondary-container text-on-secondary-container"
                ><NoEntry
              /></span>
              <div class="flex-1 min-w-0">
                <h3 class="text-sm font-semibold text-on-surface">
                  {{ $t('game_settings.dnd_mode') }}
                </h3>
                <p class="text-xs text-on-surface-variant mt-0.5">
                  {{ $t('game_settings.dnd_mode_description') }}
                </p>
              </div>
              <ToggleSwitch
                :model-value="settings.isEnabled && settings.enable_dnd"
                :disabled="!settings.isEnabled"
                @update:model-value="(v) => setOption('enable_dnd', v)"
              />
            </div>
          </div>
        </div>

        <!-- Play statistics from recorded sessions -->
        <h2 class="text-sm font-semibold text-primary px-4 pt-5 pb-2">
          {{ $t('game_settings.stats_title') }}
        </h2>
        <div v-if="!stats" class="m3-card p-5 text-sm text-on-surface-variant mb-8">
          {{ $t('game_settings.no_stats') }}
        </div>
        <template v-else>
          <div class="grid grid-cols-2 gap-2 mb-2">
            <div class="tile bg-surface-container col-span-2 flex items-end justify-between">
              <div>
                <p class="text-xs text-on-surface-variant">{{ $t('game_settings.play_time') }}</p>
                <p class="m3-headline text-4xl text-on-surface">
                  {{ formatDuration(stats.totalSeconds) }}
                </p>
              </div>
              <p class="text-xs text-on-surface-variant text-right">
                {{ $t('game_settings.sessions_count', stats.count) }}<br />
                {{ $t('games_page.last_played', { when: relativeTime(stats.lastStart, locale) }) }}
              </p>
            </div>
            <div class="tile bg-primary-container text-on-primary-container">
              <p class="text-xs opacity-80">{{ $t('sessions.avg_fps') }}</p>
              <p class="m3-headline text-3xl tabular-nums">{{ fmt(stats.fpsAvg) }}</p>
            </div>
            <div class="tile bg-tertiary-container text-on-tertiary-container">
              <p class="text-xs opacity-80">{{ $t('game_settings.hottest_cpu') }}</p>
              <p class="m3-headline text-3xl tabular-nums">
                {{ stats.cpuMax === null ? '–' : `${fmt(stats.cpuMax, 1)}°` }}
              </p>
            </div>
          </div>

          <div class="pb-8">
            <div v-for="s in stats.sessions.slice(0, 10)" :key="s.id" class="md3-list">
              <RippleComponent
                class="md3-list-item"
                tabindex="0"
                @click="router.push(`/monitor/session/${s.id}`)"
              >
                <div class="flex items-center gap-3 px-5 py-3.5">
                  <div class="flex-1 min-w-0">
                    <p class="text-sm font-semibold text-on-surface">{{ formatDate(s.start) }}</p>
                    <p class="text-xs text-on-surface-variant">
                      {{ formatDuration(s.duration) }} ·
                      {{ $t('sessions.drops_count', s.summary.drops) }}
                    </p>
                  </div>
                  <p class="m3-headline text-xl tabular-nums text-on-surface">
                    {{ fmt(s.summary.fps_avg) }}
                  </p>
                  <ChevronRightIcon
                    class="text-on-surface-variant shrink-0 rtl:rotate-180"
                    :size="20"
                  />
                </div>
              </RippleComponent>
            </div>
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useGamesStore } from '@/stores/Games'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useSessionsStore, formatDuration, fmt, relativeTime } from '@/stores/Sessions'
import * as KernelSU from '@/helpers/KernelSU'

import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'
import RippleComponent from '@/components/ui/Ripple.vue'
import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import ChevronRightIcon from '@/components/icons/ChevronRight.vue'
import Candy from '@/components/icons/Candy.vue'
import Feather from '@/components/icons/Feather.vue'
import NoEntry from '@/components/icons/NoEntry.vue'
import InformationOutline from '@/components/icons/InformationOutline.vue'
import OpenInNew from '@/components/icons/OpenInNew.vue'

const route = useRoute()
const router = useRouter()
const { locale } = useI18n()
const gamesStore = useGamesStore()
const fluxConfigStore = useFluxConfigStore()
const sessions = useSessionsStore()

const currentApp = ref({})
const globalLite = ref(false)

// Live view of this game's entry in gamelist.json (fluxd watches the file).
const settings = computed(() => {
  const pkg = currentApp.value.packageName
  const cfg = gamesStore.gamelistConfig[pkg]
  return { isEnabled: !!cfg, lite_mode: !!cfg?.lite_mode, enable_dnd: !!cfg?.enable_dnd }
})
const stats = computed(() => sessions.statsFor(currentApp.value.packageName))

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    globalLite.value = fluxConfigStore.isLiteModeEnabled
  } catch {
    globalLite.value = false
  }
  if (!sessions.historyLoaded) sessions.loadHistory()
})

watch(
  () => route.params.packageName,
  (pkg) => pkg && loadApp(pkg),
  { immediate: true },
)

async function loadApp(pkg) {
  const fromStore = gamesStore.userApps.find((a) => a.packageName === pkg)
  if (fromStore) {
    currentApp.value = fromStore
    return
  }
  currentApp.value = { packageName: pkg, appName: pkg, icon: '/app_icon_fallback.avif' }
  const [label, icon] = await Promise.allSettled([
    KernelSU.getAppLabel(pkg),
    KernelSU.getAppIcon(pkg, 128),
  ])
  currentApp.value = {
    packageName: pkg,
    appName: label.status === 'fulfilled' ? label.value : pkg,
    icon: icon.status === 'fulfilled' && icon.value ? icon.value : '/app_icon_fallback.avif',
  }
  if (!Object.keys(gamesStore.gamelistConfig).length) await gamesStore.loadGamelistConfig?.()
}

// Every change is written right away: closing the WebUI never loses it.
async function setEnabled(enabled) {
  try {
    await gamesStore.toggleAppEnabled(currentApp.value.packageName, enabled)
  } catch (error) {
    console.error('Failed to update game list:', error)
  }
}

async function setOption(key, value) {
  if (!settings.value.isEnabled) return
  try {
    await gamesStore.updateAppSetting(currentApp.value.packageName, key, value)
  } catch (error) {
    console.error(`Failed to set ${key}:`, error)
  }
}

const launch = () =>
  currentApp.value.packageName && KernelSU.launchApp(currentApp.value.packageName)
const appInfo = () =>
  currentApp.value.packageName && KernelSU.openAppInfo(currentApp.value.packageName)
const iconError = (e) => (e.target.src = '/app_icon_fallback.avif')
const formatDate = (ms) =>
  new Date(ms).toLocaleString([], {
    day: 'numeric',
    month: 'short',
    hour: '2-digit',
    minute: '2-digit',
  })
</script>

<style scoped>
.hero-icon {
  width: 104px;
  height: 104px;
  display: grid;
  place-items: center;
  overflow: hidden;
  transition: clip-path var(--m3-spring-slow-spatial-duration) var(--m3-spring-default-spatial);
}

.hero-icon:active {
  clip-path: var(--m3-shape-burst);
}

.hero-icon img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 10px 18px;
  border-radius: 999px;
  font-size: 14px;
  font-weight: 650;
}

.main-card {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 18px 20px;
  border-radius: 28px;
  transition: background-color var(--m3-spring-default-effects-duration)
    var(--m3-spring-default-effects);
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

.tile {
  border-radius: 24px;
  padding: 16px;
}
</style>
