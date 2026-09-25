<template>
  <div class="page log-level-selection-page h-full flex flex-col overflow-hidden bg-surface">
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
        <h1 class="m3-headline text-4xl text-on-surface mt-8 mb-2 px-1">
          {{ $t('log_level_selection.title') }}
        </h1>
        <p class="text-sm text-on-surface-variant px-1 mb-5">
          {{ $t('log_level_selection.brief') }}
        </p>

        <!-- Levels -->
        <div role="radiogroup" :aria-label="$t('log_level_selection.title')" class="mb-6">
          <div v-for="level in logLevels" :key="level.value" class="md3-list">
            <RippleComponent
              class="md3-list-item level-item"
              :class="{ 'is-selected': selectedLevel === level.value }"
              tabindex="0"
              role="radio"
              :aria-checked="selectedLevel === level.value"
              @click="selectLogLevel(level.value)"
            >
              <div class="flex items-center gap-4 px-5 py-3.5">
                <span class="level-badge shape-cookie9" :class="level.tone">{{
                  level.letter
                }}</span>
                <div class="flex-1 min-w-0">
                  <div class="flex items-center gap-2 flex-wrap">
                    <h3 class="text-sm font-semibold text-on-surface">
                      {{ $t(`log_level_selection.level_${level.value}`) }}
                    </h3>
                    <span v-if="level.tag" class="tag">{{
                      $t(`log_level_selection.tag.${level.tag}`)
                    }}</span>
                  </div>
                  <p class="text-xs text-on-surface-variant mt-0.5">
                    {{ $t(`log_level_selection.desc_${level.value}`) }}
                  </p>
                </div>
                <span
                  class="radio"
                  :class="{ on: selectedLevel === level.value }"
                  aria-hidden="true"
                ></span>
              </div>
            </RippleComponent>
          </div>
        </div>

        <!-- Log viewer -->
        <section class="m3-card p-4 mb-3">
          <div class="flex items-center justify-between gap-2 mb-3">
            <h2 class="text-sm font-semibold">{{ $t('log_level_selection.viewer_title') }}</h2>
            <button
              class="m3-press text-xs font-semibold text-primary px-3 py-1.5 rounded-full hover:bg-surface-container-high"
              @click="loadLog"
            >
              {{ $t('log_level_selection.refresh') }}
            </button>
          </div>
          <div class="flex gap-1.5 mb-3">
            <button
              v-for="f in filters"
              :key="f"
              class="filter-chip m3-press"
              :class="{ on: filter === f }"
              :aria-pressed="filter === f"
              @click="filter = f"
            >
              {{ $t(`log_level_selection.filter.${f}`) }}
            </button>
          </div>
          <div ref="logBox" class="log-box scrollbar-hidden">
            <p v-if="!shownLines.length" class="text-xs text-on-surface-variant p-3">
              {{ logLoaded ? $t('log_level_selection.empty') : $t('common.loading') }}
            </p>
            <div v-for="(l, i) in shownLines" :key="i" class="log-line">
              <span class="time">{{ l.time }}</span>
              <span class="lvl" :class="`lvl-${l.level}`">{{ l.level }}</span>
              <span class="msg allow-copy">{{ l.msg }}</span>
            </div>
          </div>
          <p class="text-[11px] text-on-surface-variant mt-2">
            {{ $t('log_level_selection.viewer_note') }}
          </p>
        </section>

        <!-- Save log -->
        <section class="m3-card p-4 mb-8">
          <div class="flex items-center gap-4">
            <span class="save-badge shape-flower bg-primary-container text-on-primary-container"
              ><ContentSaveIcon
            /></span>
            <div class="flex-1 min-w-0">
              <h2 class="text-sm font-semibold">{{ $t('settings_page.save_log.title') }}</h2>
              <p class="text-xs text-on-surface-variant mt-0.5">
                {{ $t('log_level_selection.save_contents') }}
              </p>
            </div>
          </div>
          <button
            class="m3-press m3-press-morph w-full mt-4 rounded-full py-3 text-sm font-semibold bg-primary text-on-primary disabled:opacity-60"
            :disabled="saveStatus === 'loading'"
            @click="saveLog"
          >
            {{
              saveStatus === 'loading'
                ? $t('settings_page.save_log.exporting')
                : $t('log_level_selection.save_button')
            }}
          </button>
          <div
            v-if="saveStatus === 'success'"
            class="result bg-secondary-container text-on-secondary-container"
          >
            <p class="text-sm font-semibold">{{ $t('settings_page.save_log.success') }}</p>
            <p class="allow-copy text-xs mt-1 break-all font-mono">{{ savedPath }}</p>
          </div>
          <div
            v-else-if="saveStatus === 'error'"
            class="result bg-error-container text-on-error-container"
          >
            <p class="text-sm font-semibold">{{ $t('settings_page.save_log.failure') }}</p>
          </div>
        </section>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, nextTick, onMounted, onUnmounted, onActivated, onDeactivated } from 'vue'
import { useRouter } from 'vue-router'
import { exec } from 'kernelsu'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import { useI18n } from 'vue-i18n'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import ContentSaveIcon from '@/components/icons/ContentSave.vue'
import RippleComponent from '@/components/ui/Ripple.vue'

const LOG_FILE = '/data/adb/.config/flux/flux.log'
const UTILITY = '/data/adb/modules/flux/system/bin/flux_utility'
const LINES = 200

const router = useRouter()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()
const { t } = useI18n()

// Mirrors FluxLog::set_log_level: 0 critical .. 5 trace. Letters match the log's level column.
const logLevels = [
  { value: 0, letter: 'C', tone: 'bg-error text-on-error' },
  { value: 1, letter: 'E', tone: 'bg-error-container text-on-error-container' },
  { value: 2, letter: 'W', tone: 'bg-tertiary-container text-on-tertiary-container' },
  {
    value: 3,
    letter: 'I',
    tone: 'bg-primary-container text-on-primary-container',
    tag: 'recommended',
  },
  {
    value: 4,
    letter: 'D',
    tone: 'bg-secondary-container text-on-secondary-container',
    tag: 'bug_reports',
  },
  { value: 5, letter: 'T', tone: 'bg-surface-container-highest text-on-surface' },
]

const selectedLevel = ref(3)

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    selectedLevel.value = fluxConfigStore.logLevel
  } catch (error) {
    console.error('Failed to load log level:', error)
  }
})

// fluxd watches config.json and applies the level immediately.
async function selectLogLevel(level) {
  if (level === selectedLevel.value) return
  // Trace logs every decision: the file grows fast and costs a little CPU.
  if (level === 5) {
    const ok = await notify.confirm({
      tone: 'warning',
      title: t('log_level_selection.trace_confirm.title'),
      message: t('log_level_selection.trace_confirm.message'),
      confirmText: t('common.enable'),
    })
    if (!ok) return
  }
  const previous = selectedLevel.value
  selectedLevel.value = level
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setLogLevel(level)
    await fluxConfigStore.saveConfig()
    notify.success(t('log_level_selection.saved'))
    setTimeout(loadLog, 600)
  } catch (error) {
    console.error('Failed to save log level:', error)
    selectedLevel.value = previous
    notify.error(t('notify.save_failed'))
  }
}

// ── Log viewer ──────────────────────────────────────────────────────────────

const filters = ['all', 'warn', 'error']
const filter = ref('all')
const lines = ref([])
const logLoaded = ref(false)
const logBox = ref(null)
const LINE_RE = /^\d{4}-\d{2}-\d{2} (\d{2}:\d{2}:\d{2})\.\d+ ([TDIWEC]) (.*)$/

const shownLines = computed(() => {
  if (filter.value === 'error') return lines.value.filter((l) => l.level === 'E' || l.level === 'C')
  if (filter.value === 'warn') return lines.value.filter((l) => 'WEC'.includes(l.level))
  return lines.value
})

async function loadLog() {
  try {
    const { stdout } = await exec(`tail -n ${LINES} ${LOG_FILE} 2>/dev/null`)
    lines.value = (stdout || '')
      .split('\n')
      .filter(Boolean)
      .map((raw) => {
        const m = raw.match(LINE_RE)
        return m ? { time: m[1], level: m[2], msg: m[3] } : { time: '', level: 'I', msg: raw }
      })
  } catch (error) {
    console.error('Failed to read log:', error)
  }
  logLoaded.value = true
  await nextTick()
  if (logBox.value) logBox.value.scrollTop = logBox.value.scrollHeight
}

let timer = null
function startTail() {
  stopTail()
  loadLog()
  timer = setInterval(loadLog, 3000)
}
function stopTail() {
  if (timer) clearInterval(timer)
  timer = null
}
onMounted(startTail)
onActivated(startTail)
onDeactivated(stopTail)
onUnmounted(stopTail)

// ── Save log (bug report bundle) ────────────────────────────────────────────

const saveStatus = ref('idle')
const savedPath = ref('')

async function saveLog() {
  saveStatus.value = 'loading'
  savedPath.value = ''
  try {
    const { errno, stdout } = await exec(`${UTILITY} save_logs`)
    const path = (stdout || '').trim().split('\n').pop()
    if (errno === 0 && path) {
      savedPath.value = path
      saveStatus.value = 'success'
    } else {
      saveStatus.value = 'error'
    }
  } catch (error) {
    console.error('Failed to save log:', error)
    saveStatus.value = 'error'
  }
}

function goBack() {
  router.back()
}
</script>

<style scoped>
.level-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
  font-weight: 800;
  font-size: 15px;
}

.level-item.is-selected {
  background: var(--color-surface-container-highest) !important;
  box-shadow: inset 0 0 0 2px var(--color-primary);
}

.tag {
  font-size: 10px;
  font-weight: 700;
  padding: 2px 8px;
  border-radius: 999px;
  background: var(--color-primary);
  color: var(--color-on-primary);
}

.radio {
  width: 22px;
  height: 22px;
  border-radius: 999px;
  border: 2px solid var(--color-on-surface-variant);
  flex-shrink: 0;
  position: relative;
  transition: border-color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.radio::after {
  content: '';
  position: absolute;
  inset: 3px;
  border-radius: 999px;
  background: var(--color-primary);
  transform: scale(0);
  transition: transform var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial);
}

.radio.on {
  border-color: var(--color-primary);
}

.radio.on::after {
  transform: scale(1);
}

.filter-chip {
  font-size: 12px;
  font-weight: 600;
  padding: 6px 12px;
  border-radius: 8px;
  border: 1px solid var(--color-outline-variant);
  color: var(--color-on-surface-variant);
  transition:
    background-color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects),
    border-radius var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial);
}

.filter-chip.on {
  background: var(--color-secondary-container);
  color: var(--color-on-secondary-container);
  border-color: transparent;
  border-radius: 999px;
}

.log-box {
  max-height: 340px;
  overflow-y: auto;
  border-radius: 16px;
  background: var(--color-surface-container-lowest);
  padding: 8px 4px;
  font-family: ui-monospace, 'SFMono-Regular', Menlo, Consolas, monospace;
}

.log-line {
  display: grid;
  grid-template-columns: auto auto 1fr;
  gap: 8px;
  padding: 2px 8px;
  font-size: 11px;
  line-height: 1.45;
}

.log-line .time {
  color: var(--color-on-surface-variant);
  opacity: 0.8;
}

.log-line .msg {
  color: var(--color-on-surface);
  word-break: break-word;
}

.lvl {
  font-weight: 800;
  width: 14px;
  text-align: center;
}

.lvl-T,
.lvl-D {
  color: var(--color-on-surface-variant);
}

.lvl-I {
  color: var(--color-primary);
}

.lvl-W {
  color: var(--color-tertiary);
}

.lvl-E,
.lvl-C {
  color: var(--color-error);
}

.save-badge {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.result {
  margin-top: 12px;
  padding: 12px 14px;
  border-radius: 16px;
}
</style>
