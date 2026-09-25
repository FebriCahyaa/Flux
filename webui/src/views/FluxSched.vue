<template>
  <div class="page h-full flex flex-col overflow-hidden bg-surface">
    <div class="max-w-3xl mx-auto h-full flex flex-col w-full">
      <div class="flex-none p-5 pb-3">
        <div class="flex items-center gap-4 mb-2">
          <button @click="goBack" class="text-on-surface transition-colors">
            <ArrowLeftIcon class="w-6 h-6 cursor-pointer rtl:rotate-180" />
          </button>
        </div>
      </div>

      <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll px-5">
        <div class="space-y-6">
          <h1 class="text-4xl text-on-surface mt-12 mb-6">
            {{ $t('flux_sched.title') }}
          </h1>

          <div class="bg-primary-container rounded-3xl p-5 -mx-1.5">
            <div class="flex items-center justify-between">
              <h2 class="text-base font-medium text-on-primary-container">
                {{ $t('flux_sched.toggle_title') }}
              </h2>
              <ToggleSwitch v-model="isFluxSchedEnabled" @update:modelValue="toggleFluxSched" />
            </div>
          </div>

          <!-- Kernel support: uclamp needs /dev/cpuctl/top-app/cpu.uclamp.min -->
          <div
            v-if="isSupported !== null"
            class="rounded-2xl px-4 py-3 text-xs -mx-1.5"
            :class="isSupported
              ? 'bg-secondary-container text-on-secondary-container'
              : 'bg-error-container text-on-error-container'"
          >
            {{ isSupported ? $t('flux_sched.supported') : $t('flux_sched.unsupported') }}
          </div>

          <InformationOutlineIcon class="text-on-surface-variant my-6" :size="22" />
          <p class="text-sm text-on-surface-variant leading-relaxed">
            {{ $t('flux_sched.brief') }}
          </p>
          <p class="text-sm text-on-surface-variant leading-relaxed">
            {{ $t('flux_sched.apply_note') }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import { useI18n } from 'vue-i18n'
import { exec } from 'kernelsu'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'

const UCLAMP_NODE = '/dev/cpuctl/top-app/cpu.uclamp.min'

const router = useRouter()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()
const { t } = useI18n()

const isFluxSchedEnabled = ref(true)
const isSupported = ref(null) // null = not checked yet

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) {
      await fluxConfigStore.loadConfig()
    }
    isFluxSchedEnabled.value = fluxConfigStore.isFluxSchedEnabled
  } catch (error) {
    console.error('Failed to load Flux Sched setting:', error)
  }

  try {
    const { errno } = await exec(`test -f ${UCLAMP_NODE}`)
    isSupported.value = errno === 0
  } catch (error) {
    console.error('Failed to check uclamp support:', error)
  }
})

async function toggleFluxSched(enabled) {
  isFluxSchedEnabled.value = enabled

  try {
    if (!fluxConfigStore.isLoaded) {
      await fluxConfigStore.loadConfig()
    }
    fluxConfigStore.setFluxSched(enabled)
    await fluxConfigStore.saveConfig()
    notify.success(
      t(enabled ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', { name: 'Flux Sched' }),
    )
  } catch (error) {
    console.error('Failed to set Flux Sched:', error)
    isFluxSchedEnabled.value = fluxConfigStore.isFluxSchedEnabled
    notify.error(t('notify.save_failed'))
  }
}

function goBack() {
  router.back()
}
</script>
