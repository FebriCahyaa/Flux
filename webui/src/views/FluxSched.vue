<template>
  <SettingsDetailLayout
    :title="$t('flux_sched.title')"
    :description="$t('settings_page.flux_sched.description')"
    :icon="TuneIcon"
    shape="shape-pentagon"
    tone="bg-secondary-container text-on-secondary-container"
    :read-error="readError"
  >
    <SettingsSwitch
      variant="main"
      :title="$t('flux_sched.toggle_title')"
      :description="supportText"
      :model-value="isFluxSchedEnabled"
      :busy="saving"
      :disabled="!ready"
      @update:model-value="toggleFluxSched"
    />

    <!-- Kernel support (uclamp needs /dev/cpuctl/top-app/cpu.uclamp.min).
         Only shown as a warning when the probe actually says no. -->
    <SettingsNote v-if="isSupported === false" tone="warning">
      {{ $t('flux_sched.unsupported') }}
    </SettingsNote>

    <!-- Technical detail: secondary to the switch above. -->
    <SettingsNote>
      {{ $t('flux_sched.brief') }}
      <span class="block mt-2">{{ $t('flux_sched.apply_note') }}</span>
    </SettingsNote>
  </SettingsDetailLayout>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import { useI18n } from 'vue-i18n'
import { exec } from 'kernelsu'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsSwitch from '@/components/ui/SettingsSwitch.vue'
import SettingsNote from '@/components/ui/SettingsNote.vue'
import TuneIcon from '@/components/icons/Tune.vue'

const UCLAMP_NODE = '/dev/cpuctl/top-app/cpu.uclamp.min'

const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()
const { t } = useI18n()

const isFluxSchedEnabled = ref(fluxConfigStore.isFluxSchedEnabled)
const isSupported = ref(null) // null = unknown
const probed = ref(false) // the check has finished (it may still be unknown if it failed)
const ready = ref(false)
const readError = ref(false)
const saving = ref(false)

// Under the switch: whether this kernel can use it. Nothing is claimed until
// the probe has answered; "not supported" is shown as the warning below.
const supportText = computed(() => {
  if (isSupported.value === true) return t('flux_sched.supported')
  if (isSupported.value === false || probed.value) return ''
  return t('common.loading')
})

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) {
      await fluxConfigStore.loadConfig()
    }
  } catch (error) {
    console.error('Failed to load Flux Sched setting:', error)
    readError.value = true
  }
  isFluxSchedEnabled.value = fluxConfigStore.isFluxSchedEnabled
  ready.value = true

  try {
    const { errno } = await exec(`test -f ${UCLAMP_NODE}`)
    isSupported.value = errno === 0
  } catch (error) {
    console.error('Failed to check uclamp support:', error)
  }
  probed.value = true
})

async function toggleFluxSched(enabled) {
  if (saving.value) return
  saving.value = true
  isFluxSchedEnabled.value = enabled
  try {
    await fluxConfigStore.commit(() => fluxConfigStore.setFluxSched(enabled))
    readError.value = false
    notify.success(
      t(enabled ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t('flux_sched.title'),
      }),
    )
  } catch (error) {
    console.error('Failed to set Flux Sched:', error)
    notify.error(t('notify.save_failed'))
  } finally {
    isFluxSchedEnabled.value = fluxConfigStore.isFluxSchedEnabled
    saving.value = false
  }
}
</script>
