import { defineStore } from 'pinia'
import { ref } from 'vue'
import { exec } from 'kernelsu'

const UTILITY = '/data/adb/modules/flux/system/bin/flux_utility'

/**
 * What this device and kernel support (`flux_utility capabilities`), so the
 * WebUI can hide switches that would do nothing here. Until the probe answers,
 * or when it fails, everything counts as supported.
 */
export const useCapabilitiesStore = defineStore('capabilities', () => {
  const caps = ref(null)
  let pending = null

  function load(force = false) {
    if (caps.value && !force) return Promise.resolve(caps.value)
    if (pending) return pending
    pending = exec(`${UTILITY} capabilities`)
      .then(({ errno, stdout }) => {
        if (errno === 0) caps.value = JSON.parse((stdout || '').trim().split('\n').pop())
      })
      .catch((error) => console.error('Failed to read capabilities:', error))
      .finally(() => (pending = null))
      .then(() => caps.value)
    return pending
  }

  /** key: one capability name or an array (any of them). */
  function supports(key) {
    if (!key || !caps.value) return true
    const keys = Array.isArray(key) ? key : [key]
    return keys.some((k) => caps.value[k] !== false)
  }

  return { caps, load, supports }
})
