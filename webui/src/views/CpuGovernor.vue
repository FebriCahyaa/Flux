<template>
  <SettingsDetailLayout
    :title="$t('cpu_governor.title')"
    :description="$t('cpu_governor.brief')"
    :icon="ChipsetIcon"
    shape="shape-cookie12"
    tone="bg-tertiary-container text-on-tertiary-container"
    :read-error="readError"
  >
        <SettingsNote tone="warning">{{ $t('cpu_governor.notice') }}</SettingsNote>

        <!-- One picker per Flux profile: these edit what Flux applies in that
             profile, not the governor running right now. -->
        <div class="space-y-3 mb-5">
          <GovernorPicker
            :busy="saving"
            :title="$t('cpu_governor.default_title')"
            :description="$t('cpu_governor.default_description')"
            :icon="TuneIcon"
            shape="shape-cookie9"
            tone="bg-primary-container text-on-primary-container"
            :options="options"
            :model-value="balanceGovernor"
            :risky-values="['performance']"
            :empty-text="$t('cpu_governor.none_found')"
            @select="(g) => choose('balance', g)"
          />
          <GovernorPicker
            :busy="saving"
            :title="$t('cpu_governor.powersave_title')"
            :description="$t('cpu_governor.powersave_description')"
            :icon="BatterySaverIcon"
            shape="shape-flower"
            tone="bg-secondary-container text-on-secondary-container"
            :options="options"
            :model-value="powersaveGovernor"
            :risky-values="['performance']"
            :empty-text="$t('cpu_governor.none_found')"
            @select="(g) => choose('powersave', g)"
          />
        </div>

        <SettingsNote>{{ $t('cpu_governor.apply_note') }}</SettingsNote>
  </SettingsDetailLayout>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import * as KernelSU from '@/helpers/KernelSU'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsNote from '@/components/ui/SettingsNote.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import GovernorPicker from '@/components/ui/GovernorPicker.vue'

const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const readError = ref(false)
const saving = ref(false)
const available = ref([])
const options = computed(() => available.value.map((g) => ({ value: g, label: g })))
const balanceGovernor = computed(() => fluxConfigStore.balanceGovernor)
const powersaveGovernor = computed(() => fluxConfigStore.powersaveGovernor)

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
  } catch (error) {
    console.error('Failed to load CPU governor settings:', error)
    readError.value = true
  }
  try {
    const file = '/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors'
    if (await KernelSU.fileExists(file)) {
      available.value = (await KernelSU.readFile(file)).trim().split(/\s+/).filter(Boolean)
    }
  } catch (error) {
    console.error('Failed to initialize CPU governor page:', error)
  }
})

// Shown once after the page transition, until "don't ask again" is ticked.
function onPageReady() {
  let hidden = false
  try {
    hidden = localStorage.getItem('hide_cpu_governor_warning') === 'true'
  } catch {
    hidden = false
  }
  if (hidden) return
  notify.confirm({
    tone: 'warning',
    title: t('common.warning'),
    message: t('cpu_governor.modal.initial_warn'),
    confirmText: t('common.ok'),
    cancelText: null,
    remember: 'cpu_governor_intro',
  })
}
defineExpose({ onPageReady })

async function choose(profile, governor) {
  if (saving.value) return
  if (governor === 'performance') {
    const ok = await notify.confirm({
      tone: 'danger',
      title: t('cpu_governor.confirm.performance_title'),
      message: t('cpu_governor.modal.performance_warning'),
      points: [t('cpu_governor.confirm.point_heat'), t('cpu_governor.confirm.point_battery')],
      confirmText: t('cpu_governor.modal.performance_warning_confirm'),
    })
    if (!ok) return
  } else if (profile === 'balance' && governor === 'powersave') {
    const ok = await notify.confirm({
      tone: 'warning',
      title: t('cpu_governor.confirm.powersave_title'),
      message: t('cpu_governor.confirm.powersave_message'),
      confirmText: t('common.apply'),
    })
    if (!ok) return
  }

  saving.value = true
  try {
    await fluxConfigStore.commit(() => {
      if (profile === 'balance') fluxConfigStore.setBalanceGovernor(governor)
      else fluxConfigStore.setPowersaveGovernor(governor)
    })
    readError.value = false
    notify.success(
      t('cpu_governor.saved', {
        governor,
        profile:
          profile === 'balance'
            ? t('cpu_governor.default_title')
            : t('cpu_governor.powersave_title'),
      }),
    )
  } catch (error) {
    console.error('Failed to apply governor:', error)
    notify.error(t('notify.save_failed'))
  } finally {
    saving.value = false
  }
}
</script>
