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

      <div
        v-if="session"
        class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll px-4"
      >
        <!-- Header -->
        <div class="flex items-center gap-4 mt-4 mb-2 px-1">
          <span class="app-icon shape-cookie12 bg-surface-container-high">
            <img
              v-if="sessions.icons[session.package]"
              :src="sessions.icons[session.package]"
              alt=""
              @error="sessions.icons[session.package] = ''"
            />
            <span v-else class="text-2xl font-bold text-on-surface-variant">{{
              appName.charAt(0)
            }}</span>
          </span>
          <div class="min-w-0">
            <h1 class="m3-headline text-3xl text-on-surface break-words">{{ appName }}</h1>
            <p class="text-xs text-on-surface-variant mt-1">{{ dateText }}</p>
          </div>
        </div>

        <!-- Verdict -->
        <div class="flex flex-wrap gap-2 px-1 mt-4 mb-4">
          <span class="verdict" :class="verdict.tone">{{ verdict.text }}</span>
          <span v-if="ranHot" class="verdict bg-error-container text-on-error-container">{{
            $t('sessions.verdict.hot')
          }}</span>
          <span class="verdict bg-surface-container-high text-on-surface">{{
            formatDuration(session.duration)
          }}</span>
        </div>

        <!-- Summary bento -->
        <section class="grid grid-cols-2 gap-2 mb-3">
          <div
            class="bento m3-enter col-span-2 bg-primary-container text-on-primary-container flex items-end justify-between"
          >
            <div>
              <p class="text-xs opacity-80">{{ $t('sessions.avg_fps') }}</p>
              <p class="m3-headline text-6xl tabular-nums">{{ fmt(s.fps_avg) }}</p>
            </div>
            <div class="text-right text-xs opacity-80 leading-5">
              <p>
                {{ $t('sessions.low1') }} <b class="tabular-nums">{{ fmt(s.fps_low1) }}</b>
              </p>
              <p>
                {{ $t('sessions.min') }} <b class="tabular-nums">{{ fmt(s.fps_min) }}</b>
              </p>
              <p>
                {{ $t('sessions.median') }} <b class="tabular-nums">{{ fmt(s.fps_median) }}</b>
              </p>
            </div>
          </div>

          <div
            class="bento m3-enter bg-surface-container text-on-surface"
            style="animation-delay: 40ms"
          >
            <p class="text-xs text-on-surface-variant">{{ $t('sessions.drops') }}</p>
            <p class="m3-headline text-3xl tabular-nums" :class="s.drops ? 'text-error' : ''">
              {{ s.drops }}
            </p>
            <p class="text-[11px] text-on-surface-variant">
              {{ $t('sessions.drop_seconds', { n: s.drop_seconds }) }}
            </p>
          </div>
          <div
            class="bento m3-enter bg-surface-container text-on-surface"
            style="animation-delay: 80ms"
          >
            <p class="text-xs text-on-surface-variant">{{ $t('sessions.stability') }}</p>
            <p class="m3-headline text-3xl tabular-nums">
              {{ s.stability === null ? '–' : `${fmt(s.stability)}%` }}
            </p>
            <p class="text-[11px] text-on-surface-variant">{{ $t('sessions.stability_hint') }}</p>
          </div>

          <div
            class="bento m3-enter bg-tertiary-container text-on-tertiary-container"
            style="animation-delay: 120ms"
          >
            <p class="text-xs opacity-80">CPU</p>
            <p class="m3-headline text-3xl tabular-nums">{{ fmt(s.cpu_max, 1) }}°</p>
            <p class="text-[11px] opacity-80">{{ $t('sessions.avg') }} {{ fmt(s.cpu_avg, 1) }}°</p>
          </div>
          <div
            class="bento m3-enter bg-secondary-container text-on-secondary-container"
            style="animation-delay: 160ms"
          >
            <p class="text-xs opacity-80">{{ $t('sessions.battery') }}</p>
            <p class="m3-headline text-3xl tabular-nums">{{ fmt(s.battery_max, 1) }}°</p>
            <p class="text-[11px] opacity-80">
              {{ $t('sessions.avg') }} {{ fmt(s.battery_avg, 1) }}°
            </p>
          </div>

          <div
            class="bento m3-enter col-span-2 bg-surface-container text-on-surface flex items-center gap-4"
            style="animation-delay: 200ms"
          >
            <span class="badge shape-clover4 bg-tertiary-container text-on-tertiary-container"
              ><FeatherIcon
            /></span>
            <div>
              <p class="text-sm font-semibold">
                {{ $t('sessions.lite_time', { time: formatDuration(s.lite_seconds) }) }}
              </p>
              <p class="text-xs text-on-surface-variant">{{ $t('sessions.lite_hint') }}</p>
            </div>
          </div>
        </section>

        <!-- Charts -->
        <section class="m3-card m3-enter p-5 mb-3" style="animation-delay: 240ms">
          <div class="flex items-center justify-between mb-2">
            <h2 class="text-sm font-semibold">{{ $t('sessions.fps_over_time') }}</h2>
            <span class="legend"
              ><i style="background: var(--color-primary)"></i>{{ $t('sessions.avg')
              }}<i class="ms-2" style="background: var(--color-outline)"></i
              >{{ $t('sessions.min') }}</span
            >
          </div>
          <LineChart
            v-if="fpsAvg.some((v) => v !== null)"
            :height="140"
            :min="0"
            :series="[
              { values: fpsMin, color: 'var(--color-outline)', width: 1.5, opacity: 0.7 },
              { values: fpsAvg, color: 'var(--color-primary)', area: true, width: 2.5 },
            ]"
            :shade="liteShade"
            :reference="s.fps_median ? s.fps_median * 0.8 : null"
            :label="$t('sessions.fps_over_time')"
          />
          <p v-else class="text-sm text-on-surface-variant">{{ $t('sessions.fps_unavailable') }}</p>
          <p class="text-[11px] text-on-surface-variant mt-3">
            {{ $t('sessions.chart_note') }} · {{ fpsSourceLabel }}
          </p>
        </section>

        <section class="m3-card m3-enter p-5 mb-8" style="animation-delay: 280ms">
          <div class="flex items-center justify-between mb-2">
            <h2 class="text-sm font-semibold">{{ $t('sessions.temp_over_time') }}</h2>
            <span class="legend"
              ><i style="background: var(--color-error)"></i>CPU<i
                class="ms-2"
                style="background: var(--color-secondary)"
              ></i
              >{{ $t('sessions.battery') }}</span
            >
          </div>
          <LineChart
            :height="120"
            unit="°"
            :series="[
              { values: cpu, color: 'var(--color-error)', width: 2.5 },
              { values: battery, color: 'var(--color-secondary)', width: 2.5 },
            ]"
            :shade="liteShade"
            :label="$t('sessions.temp_over_time')"
          />
        </section>
      </div>

      <div
        v-else
        class="flex-1 grid place-items-center text-sm text-on-surface-variant px-8 text-center"
      >
        {{ sessions.historyLoaded ? $t('sessions.not_found') : $t('common.loading') }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useSessionsStore, formatDuration, fmt } from '@/stores/Sessions'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import FeatherIcon from '@/components/icons/Feather.vue'
import LineChart from '@/components/ui/LineChart.vue'

const route = useRoute()
const router = useRouter()
const { t } = useI18n()
const sessions = useSessionsStore()

onMounted(() => {
  if (!sessions.historyLoaded) sessions.loadHistory()
})

const session = computed(() => sessions.byId(route.params.id))
const s = computed(() => session.value?.summary ?? {})
const appName = computed(() => sessions.labels[session.value?.package] || session.value?.package)
const dateText = computed(() =>
  session.value
    ? new Date(session.value.start).toLocaleString([], { dateStyle: 'full', timeStyle: 'short' })
    : '',
)

// Timeline points: [fps_avg, fps_min, cpu_max, battery, lite_seconds]
const col = (i) => computed(() => (session.value?.timeline ?? []).map((p) => p[i]))
const fpsAvg = col(0)
const fpsMin = col(1)
const cpu = col(2)
const battery = col(3)
const liteShade = computed(() => (session.value?.timeline ?? []).map((p) => p[4] > 0))

const ranHot = computed(() => (s.value.cpu_max ?? 0) >= 80 || (s.value.battery_max ?? 0) >= 45)

const verdict = computed(() => {
  const st = s.value.stability
  if (st === null || st === undefined)
    return { text: t('sessions.verdict.no_fps'), tone: 'bg-surface-container-high text-on-surface' }
  if (st >= 90 && s.value.drops <= 2)
    return { text: t('sessions.verdict.smooth'), tone: 'bg-primary text-on-primary' }
  if (st >= 70)
    return { text: t('sessions.verdict.some_drops'), tone: 'bg-tertiary text-on-tertiary' }
  return { text: t('sessions.verdict.unstable'), tone: 'bg-error text-on-error' }
})

const fpsSourceLabel = computed(() => {
  const src = session.value?.fps_source
  return src ? t(`sessions.source.${src}`) : t('sessions.source.none')
})

function goBack() {
  router.back()
}
</script>

<style scoped>
.app-icon {
  width: 64px;
  height: 64px;
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

.verdict {
  font-size: 12px;
  font-weight: 650;
  padding: 6px 12px;
  border-radius: 999px;
}

.bento {
  border-radius: 28px;
  padding: 16px;
}

.badge {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.legend {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--color-on-surface-variant);
}

.legend i {
  display: inline-block;
  width: 12px;
  height: 3px;
  border-radius: 2px;
}
</style>
