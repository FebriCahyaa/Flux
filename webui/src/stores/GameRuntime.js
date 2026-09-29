import { defineStore } from 'pinia'
import { ref } from 'vue'
import { exec } from 'kernelsu'
import * as KernelSU from '@/helpers/KernelSU'

const FLUXD = '/data/adb/modules/flux/system/bin/fluxd'
const DIR = '/data/adb/.config/flux'
const PROFILES = `${DIR}/game_profiles.json`
const LIBRARY = `${DIR}/compat_library.json`
const ZYGISK_OPTIN = `${DIR}/compat_zygisk_optin`

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

  async function setZygiskOptIn(enabled) {
    await exec(enabled ? `touch "${ZYGISK_OPTIN}"` : `rm -f "${ZYGISK_OPTIN}"`)
    zygiskOptIn.value = enabled
  }

  return {
    profiles,
    identities,
    analysis,
    analysisStatus,
    zygiskOptIn,
    loaded,
    load,
    profileFor,
    identityNames,
    setField,
    analyze,
    setZygiskOptIn,
  }
})
