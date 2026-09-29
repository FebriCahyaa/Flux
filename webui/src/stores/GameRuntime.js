import { defineStore } from 'pinia'
import { ref } from 'vue'
import { exec } from 'kernelsu'
import * as KernelSU from '@/helpers/KernelSU'

const FLUXD = '/data/adb/modules/flux/system/bin/fluxd'
const DIR = '/data/adb/.config/flux'
const PROFILES = `${DIR}/game_profiles.json`
const LIBRARY = `${DIR}/compat_library.json`
const ZYGISK_OPTIN = `${DIR}/compat_zygisk_optin`
const STATUS = `${DIR}/compat_status.json`

// Package names are interpolated into a shell command: accept only what Android allows.
const PACKAGE_RE = /^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z0-9_-]+)+$/

/**
 * Per-game runtime profile (performance + compatibility) and the read-only
 * compatibility analysis fluxd produces for it.
 *
 * Profiles live in game_profiles.json as { "<package>": { performance, compatibility } };
 * only the fields the user changed are stored, everything else inherits.
 */
export const useGameRuntimeStore = defineStore('gameRuntime', () => {
  const profiles = ref({})
  const identities = ref({}) // name -> { layer, fields }
  const analysis = ref(null)
  const analysisStatus = ref('idle') // idle | loading | ready | error
  const zygiskOptIn = ref(false)
  // Process-context snapshot written by fluxd (compat_status.json): what is actually active right now.
  const runtimeStatus = ref(null)
  const loaded = ref(false)

  async function readJson(path, fallback) {
    try {
      if (!(await KernelSU.fileExists(path))) return fallback
      return JSON.parse(await KernelSU.readFile(path))
    } catch {
      return fallback
    }
  }

  async function load() {
    profiles.value = await readJson(PROFILES, {})
    const lib = await readJson(LIBRARY, {})
    identities.value = lib.identities || {}
    try {
      zygiskOptIn.value = await KernelSU.fileExists(ZYGISK_OPTIN)
    } catch {
      zygiskOptIn.value = false
    }
    loaded.value = true
  }

  function profileFor(pkg) {
    const p = profiles.value[pkg] || {}
    return { performance: p.performance || {}, compatibility: p.compatibility || {} }
  }

  /** Identity profile names for one layer ('device' | 'cpu' | 'gpu'). */
  function identityNames(layer) {
    return Object.entries(identities.value)
      .filter(([, v]) => v.layer === layer)
      .map(([name]) => name)
  }

  /**
   * Set one field. `value` null/'' removes the override so the game inherits again.
   * Rolls the in-memory copy back when the write fails.
   */
  async function setField(pkg, section, key, value) {
    if (!PACKAGE_RE.test(pkg)) throw new Error('invalid package')
    const previous = profiles.value
    const current = previous[pkg] || {}
    const nextSection = { ...(current[section] || {}) }
    if (value === null || value === '' || value === undefined) delete nextSection[key]
    else nextSection[key] = value
    const nextGame = { ...current, package: pkg, [section]: nextSection }
    const next = { ...previous, [pkg]: nextGame }
    profiles.value = next
    try {
      await KernelSU.writeFile(PROFILES, JSON.stringify(next, null, 2))
    } catch (e) {
      profiles.value = previous
      throw e
    }
    // Compatibility edits change what the provider must be armed with.
    if (section === 'compatibility') await armNow()
  }

  /** Runs `fluxd compat_analyze`; changes nothing on the device. */
  async function analyze(pkg, mode) {
    if (!PACKAGE_RE.test(pkg)) return
    analysisStatus.value = 'loading'
    try {
      const args = mode && /^[a-z]+$/.test(mode) ? ` ${mode}` : ''
      const { errno, stdout } = await exec(`${FLUXD} compat_analyze ${pkg}${args}`)
      if (errno !== 0) throw new Error('analysis failed')
      const parsed = JSON.parse((stdout || '').trim().split('\n').pop())
      if (!parsed.ok) throw new Error(parsed.error || 'analysis failed')
      analysis.value = parsed
      analysisStatus.value = 'ready'
    } catch (e) {
      console.error('compat_analyze failed:', e)
      analysis.value = null
      analysisStatus.value = 'error'
    }
  }

  /** Live state of the running game session, or null when none / unreadable. */
  async function loadStatus() {
    runtimeStatus.value = await readJson(STATUS, null)
    return runtimeStatus.value
  }

  /**
   * Ask the daemon to re-arm the provider's plans. Identity is set when a game process is created,
   * so a profile edit only takes effect for the next launch, and only once the plan is armed.
   * Best effort: an old daemon without the command, or no daemon, just means "not armed yet".
   */
  async function armNow() {
    try {
      await exec(`${FLUXD} compat_arm`)
    } catch (e) {
      console.error('compat_arm failed:', e)
    }
  }

  async function setZygiskOptIn(enabled) {
    await exec(enabled ? `touch "${ZYGISK_OPTIN}"` : `rm -f "${ZYGISK_OPTIN}"`)
    zygiskOptIn.value = enabled
    await armNow() // consent withdrawn => the daemon disarms every plan
  }

  return {
    profiles,
    identities,
    analysis,
    analysisStatus,
    zygiskOptIn,
    runtimeStatus,
    loaded,
    load,
    profileFor,
    identityNames,
    setField,
    analyze,
    loadStatus,
    armNow,
    setZygiskOptIn,
  }
})
