import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import * as KernelSU from '@/helpers/KernelSU'

// Written by fluxd's SessionRecorder (jni/SessionRecorder.cpp)
const LIVE_FILE = '/data/adb/.config/flux/session_live.json'
const HISTORY_FILE = '/data/adb/.config/flux/sessions.json'

/**
 * Game sessions: the live one (play time, FPS, temperatures, updated every
 * second while a game runs) and the history of finished sessions.
 */
export const useSessionsStore = defineStore('sessions', () => {
  const live = ref(null) // parsed live file while a session runs, otherwise null
  const history = ref([])
  const historyLoaded = ref(false)
  const labels = ref({}) // package -> app name
  const icons = ref({}) // package -> icon URL

  const liveActive = computed(() => !!live.value?.active)

  async function readLive() {
    let next = null
    try {
      const data = JSON.parse(await KernelSU.readFile(LIVE_FILE))
      if (data && data.active) next = data
    } catch {
      next = null
    }
    const ended = liveActive.value && !next
    live.value = next
    if (next) resolveApps([next.package])
    // A session just finished: it is now in the history.
    if (ended) await loadHistory()
  }

  async function loadHistory() {
    try {
      const data = JSON.parse(await KernelSU.readFile(HISTORY_FILE))
      history.value = Array.isArray(data) ? data.filter((s) => s && s.package && s.summary) : []
    } catch {
      history.value = []
    }
    historyLoaded.value = true
    resolveApps(history.value.map((s) => s.package))
  }

  async function resolveApps(packages) {
    const missing = [...new Set(packages)].filter((p) => p && !(p in labels.value))
    if (!missing.length) return
    for (const p of missing) labels.value[p] = p
    try {
      for (const { packageName, appName } of await KernelSU.getBatchAppLabel(missing)) {
        labels.value[packageName] = appName || packageName
      }
    } catch {
      // package names stay as labels
    }
    for (const p of missing) {
      KernelSU.getAppIcon(p, 96)
        .then((url) => (icons.value[p] = url))
        .catch(() => {})
    }
  }

  const byId = (id) => history.value.find((s) => String(s.id) === String(id))

  return { live, liveActive, history, historyLoaded, labels, icons, readLive, loadHistory, byId }
})

// ── Formatting helpers shared by the views ──────────────────────────────────

export function formatDuration(seconds) {
  const s = Math.max(0, Math.round(seconds || 0))
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const sec = s % 60
  if (h) return `${h}h ${String(m).padStart(2, '0')}m`
  if (m) return `${m}m ${String(sec).padStart(2, '0')}s`
  return `${sec}s`
}

export function fmt(value, digits = 0) {
  return value === null || value === undefined || Number.isNaN(value)
    ? '–'
    : Number(value).toFixed(digits)
}

/** Colour role for a temperature: calm below 40 °C, warm to 45 (battery) / 75 (CPU), hot above. */
export function tempTone(value, kind = 'cpu') {
  if (value === null || value === undefined) return 'text-on-surface-variant'
  const [warm, hot] = kind === 'battery' ? [40, 45] : [65, 80]
  if (value >= hot) return 'text-error'
  if (value >= warm) return 'text-tertiary'
  return 'text-primary'
}
