import { defineStore } from 'pinia'
import { ref } from 'vue'

const REMEMBER_PREFIX = 'flux_confirm_skip_'

/**
 * App-wide feedback:
 *  - snackbars: short messages after an action ("Saved, applies on the next profile")
 *  - confirm dialogs: asked before an action with side effects, with an info,
 *    warning or danger tone. `await notify.confirm({...})` resolves to true/false.
 */
export const useNotifyStore = defineStore('notify', () => {
  const snackbars = ref([])
  const dialog = ref(null)
  let nextId = 0
  let resolveDialog = null

  function show(message, { tone = 'info', timeout = 3200, action = null } = {}) {
    const id = ++nextId
    // Only the latest two stay visible; older ones would just stack up.
    snackbars.value = [...snackbars.value.slice(-1), { id, message, tone, action }]
    if (timeout) setTimeout(() => dismiss(id), timeout)
    return id
  }

  const success = (message, opts) => show(message, { ...opts, tone: 'success' })
  const warn = (message, opts) => show(message, { ...opts, tone: 'warning' })
  const error = (message, opts) => show(message, { timeout: 5000, ...opts, tone: 'error' })

  function dismiss(id) {
    snackbars.value = snackbars.value.filter((s) => s.id !== id)
  }

  function remembered(key) {
    try {
      return !!key && localStorage.getItem(REMEMBER_PREFIX + key) === '1'
    } catch {
      return false
    }
  }

  /**
   * @param {object} opts
   * @param {'info'|'warning'|'danger'} [opts.tone]
   * @param {string} opts.title
   * @param {string} [opts.message]
   * @param {string[]} [opts.points] bullet list under the message
   * @param {string} [opts.confirmText]
   * @param {string} [opts.cancelText] null hides the cancel button
   * @param {string} [opts.remember] key for a "don't ask again" checkbox
   */
  function confirm(opts) {
    if (remembered(opts.remember)) return Promise.resolve(true)
    // A dialog already open is answered "no" before the next one replaces it.
    if (resolveDialog) resolveDialog(false)
    dialog.value = { tone: 'info', ...opts }
    return new Promise((resolve) => (resolveDialog = resolve))
  }

  function answer(ok, dontAskAgain = false) {
    if (ok && dontAskAgain && dialog.value?.remember) {
      try {
        localStorage.setItem(REMEMBER_PREFIX + dialog.value.remember, '1')
      } catch {
        // storage unavailable: ask again next time
      }
    }
    dialog.value = null
    const resolve = resolveDialog
    resolveDialog = null
    resolve?.(ok)
  }

  return { snackbars, dialog, show, success, warn, error, dismiss, confirm, answer }
})
