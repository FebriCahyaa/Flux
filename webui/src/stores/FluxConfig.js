import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

import { exec } from 'kernelsu'
import * as KernelSU from '@/helpers/KernelSU'

import { useHomeStore } from '@/stores/Home'

export const useFluxConfigStore = defineStore('fluxConfig', () => {
  const config = ref(null)

  const homeStore = useHomeStore()
  const currentProfile = computed(() => homeStore.currentProfileRaw)

  const preferences = computed(() => config.value?.preferences)
  const cpuGovernor = computed(() => config.value?.cpu_governor)

  const isLiteModeEnabled = computed(() => config.value?.preferences?.enforce_lite_mode ?? false)
  const logLevel = computed(() => config.value?.preferences?.log_level ?? 3)
  const isDeviceMitigationEnabled = computed(
    () => config.value?.preferences?.use_device_mitigation ?? false,
  )
  const isDisableTweaksEnabled = computed(() => config.value?.preferences?.disable_tweaks ?? false)
  // Missing key = enabled, matching the daemon default
  const isFluxSchedEnabled = computed(() => config.value?.preferences?.flux_sched ?? true)
  // Flux Boost parts (fluxd defaults: on)
  const fluxBoost = computed(() => ({
    flux_vm: config.value?.preferences?.flux_vm ?? true,
    flux_io: config.value?.preferences?.flux_io ?? true,
    game_priority: config.value?.preferences?.game_priority ?? true,
    sustained_mode: config.value?.preferences?.sustained_mode ?? true,
  }))
  // Game tweaks (fluxd defaults: network, touch and cache drop on; refresh rate off)
  const gameTweaks = computed(() => ({
    net_tweaks: config.value?.preferences?.net_tweaks ?? true,
    touch_tweaks: config.value?.preferences?.touch_tweaks ?? true,
    game_refresh_rate: config.value?.preferences?.game_refresh_rate ?? false,
    drop_caches: config.value?.preferences?.drop_caches ?? true,
    surface_boost: config.value?.preferences?.surface_boost ?? true,
    chipset_boost: config.value?.preferences?.chipset_boost ?? true,
    render_boost: config.value?.preferences?.render_boost ?? true,
    render_realtime: config.value?.preferences?.render_realtime ?? false,
    gpu_power_lock: config.value?.preferences?.gpu_power_lock ?? false,
    adreno_reflex: config.value?.preferences?.adreno_reflex ?? false,
    graphics_tweaks: config.value?.preferences?.graphics_tweaks ?? false,
    adaptive_refresh: config.value?.preferences?.adaptive_refresh ?? false,
    zram_tune: config.value?.preferences?.zram_tune ?? false,
  }))
  // Empty = keep the kernel's own GPU governor
  const gpuGovernor = computed(() => ({
    balance: config.value?.gpu_governor?.balance ?? '',
    powersave: config.value?.gpu_governor?.powersave ?? '',
  }))
  const balanceGovernor = computed(() => config.value?.cpu_governor?.balance ?? 'schedutil')
  const powersaveGovernor = computed(() => config.value?.cpu_governor?.powersave ?? 'schedutil')

  const isLoaded = computed(() => config.value !== null)

  const configPath = '/data/adb/.config/flux/config.json'

  async function loadConfig() {
    try {
      const content = await KernelSU.readFile(configPath)
      config.value = JSON.parse(content)
      console.log('Flux config loaded successfully')
      return config.value
    } catch (error) {
      console.error('Failed to load flux config:', error)
      config.value = null
      throw error
    }
  }

  async function saveConfig() {
    if (!config.value) {
      throw new Error('Config not loaded')
    }

    try {
      const configString = JSON.stringify(config.value, null, 2)
      await KernelSU.writeFile(configPath, configString)
      console.log('Flux config saved successfully')
      return true
    } catch (error) {
      console.error('Failed to save flux config:', error)
      throw error
    }
  }

  function ensureConfigStructure() {
    if (!config.value) {
      throw new Error('Config not loaded')
    }

    if (!config.value.preferences) {
      config.value.preferences = {}
    }
    if (!config.value.cpu_governor) {
      config.value.cpu_governor = {}
    }
    if (!config.value.gpu_governor) {
      config.value.gpu_governor = {}
    }

    if (config.value.preferences.use_device_mitigation === undefined) {
      config.value.preferences.use_device_mitigation = false
    }
    if (config.value.preferences.disable_tweaks === undefined) {
      config.value.preferences.disable_tweaks = false
    }
    if (config.value.preferences.flux_sched === undefined) {
      config.value.preferences.flux_sched = true
    }
    if (config.value.preferences.enforce_lite_mode === undefined) {
      config.value.preferences.enforce_lite_mode = false
    }
    if (config.value.preferences.log_level === undefined) {
      config.value.preferences.log_level = 5
    }
  }

  function setLiteMode(enabled) {
    ensureConfigStructure()
    config.value.preferences.enforce_lite_mode = enabled
  }

  function setLogLevel(level) {
    if (level < 0 || level > 5) {
      throw new Error('Log level must be between 0 and 5')
    }

    ensureConfigStructure()
    config.value.preferences.log_level = level
  }

  function setDeviceMitigation(enabled) {
    ensureConfigStructure()
    config.value.preferences.use_device_mitigation = enabled
  }

  function setDisableTweaks(enabled) {
    ensureConfigStructure()
    config.value.preferences.disable_tweaks = enabled
  }

  function setFluxSched(enabled) {
    ensureConfigStructure()
    config.value.preferences.flux_sched = enabled
  }

  function setFluxBoost(key, enabled) {
    if (!['flux_vm', 'flux_io', 'game_priority', 'sustained_mode'].includes(key)) return
    ensureConfigStructure()
    config.value.preferences[key] = enabled
  }

  function setGameTweak(key, enabled) {
    const keys = [
      'net_tweaks',
      'touch_tweaks',
      'game_refresh_rate',
      'drop_caches',
      'surface_boost',
      'chipset_boost',
      'render_boost',
      'render_realtime',
      'gpu_power_lock',
      'adreno_reflex',
      'graphics_tweaks',
      'adaptive_refresh',
      'zram_tune',
    ]
    if (!keys.includes(key)) return
    ensureConfigStructure()
    config.value.preferences[key] = enabled
  }

  /** profile: 'balance' | 'powersave'; governor '' restores the kernel default. */
  function setGpuGovernor(profile, governor) {
    if (!['balance', 'powersave'].includes(profile)) return
    ensureConfigStructure()
    config.value.gpu_governor[profile] = governor

    // Apply now when that profile is the active one (performance keeps the kernel's).
    const active = profile === 'balance' ? 'balanced' : 'powersave'
    if (currentProfile.value === active && /^[\w-]*$/.test(governor)) {
      exec(`/data/adb/modules/flux/system/bin/flux_utility change_gpu_gov ${governor}`).then(
        ({ errno, stderr }) => {
          if (errno !== 0) console.error('[setGpuGovernor] Failed to change GPU governor:', stderr)
        },
      )
    }
  }

  function setBalanceGovernor(governor) {
    ensureConfigStructure()
    config.value.cpu_governor.balance = governor

    if (
      currentProfile.value === 'balanced' ||
      (currentProfile.value === 'performance' && isLiteModeEnabled.value)
    ) {
      exec(`/data/adb/modules/flux/system/bin/flux_utility change_cpu_gov ${governor}`).then(({ errno, stderr }) => {
        if (errno !== 0) {
          console.error('[setBalanceGovernor] Failed to change CPU governor:', stderr)
        }
      })
    }
  }

  function setPowersaveGovernor(governor) {
    ensureConfigStructure()
    config.value.cpu_governor.powersave = governor

    if (currentProfile.value === 'powersave') {
      exec(`/data/adb/modules/flux/system/bin/flux_utility change_cpu_gov ${governor}`).then(({ errno, stderr }) => {
        if (errno !== 0) {
          console.error('[setPowersaveGovernor] Failed to change CPU governor:', stderr)
        }
      })
    }
  }

  function setCpuGovernorProfile(profile, governor) {
    if (profile === 'balance') {
      setBalanceGovernor(governor)
    } else if (profile === 'powersave') {
      setPowersaveGovernor(governor)
    } else {
      throw new Error('Invalid CPU governor profile. Must be "balance" or "powersave"')
    }
  }

  function updateConfig(newConfig) {
    if (!config.value) {
      throw new Error('Config not loaded')
    }

    config.value = {
      ...config.value,
      ...newConfig,
      preferences: {
        ...config.value.preferences,
        ...(newConfig.preferences || {}),
      },
      cpu_governor: {
        ...config.value.cpu_governor,
        ...(newConfig.cpu_governor || {}),
      },
      gpu_governor: {
        ...config.value.gpu_governor,
        ...(newConfig.gpu_governor || {}),
      },
    }
  }

  return {
    fluxBoost,
    setFluxBoost,
    gameTweaks,
    setGameTweak,
    gpuGovernor,
    setGpuGovernor,
    config,

    preferences,
    cpuGovernor,
    isLiteModeEnabled,
    logLevel,
    isDeviceMitigationEnabled,
    isDisableTweaksEnabled,
    isFluxSchedEnabled,
    balanceGovernor,
    powersaveGovernor,
    isLoaded,

    loadConfig,
    saveConfig,
    setLiteMode,
    setLogLevel,
    setDeviceMitigation,
    setDisableTweaks,
    setFluxSched,
    setBalanceGovernor,
    setPowersaveGovernor,
    setCpuGovernorProfile,
    updateConfig,
  }
})
