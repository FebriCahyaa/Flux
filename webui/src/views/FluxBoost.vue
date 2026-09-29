<template>
  <SettingsDetailLayout
    :title="$t('flux_boost.title')"
    :description="$t('flux_boost.brief')"
    :icon="BoltChargeIcon"
    shape="shape-burst"
    tone="bg-primary-container text-on-primary-container"
    :read-error="readError"
  >
    <div class="mb-6">
      <SettingsSwitch
        v-for="part in parts"
        :key="part.key"
        :title="$t(`flux_boost.${part.label}.title`)"
        :description="$t(`flux_boost.${part.label}.description`)"
        :icon="part.icon"
        :shape="part.shape"
        :badge-tone="part.tone"
        :model-value="values[part.key]"
        :busy="saving"
        :disabled="!ready"
        @update:model-value="(v) => toggle(part.key, v)"
      />
    </div>

    <SettingsNote>{{ $t('flux_boost.apply_note') }}</SettingsNote>
  </SettingsDetailLayout>
</template>

<script setup>
import { reactive, ref, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsSwitch from '@/components/ui/SettingsSwitch.vue'
import SettingsNote from '@/components/ui/SettingsNote.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import StarIcon from '@/components/icons/Star.vue'
import SpeedIcon from '@/components/icons/Speed.vue'

const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

// Config keys match FluxConfigStore::Preferences in fluxd.
const parts = [
  {
    key: 'sustained_mode',
    label: 'sustained',
    icon: SpeedIcon,
    shape: 'shape-sunny',
    tone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'flux_vm',
    label: 'vm',
    icon: ChipsetIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'flux_io',
    label: 'io',
    icon: TuneIcon,
    shape: 'shape-pentagon',
    tone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'game_priority',
    label: 'priority',
    icon: StarIcon,
    shape: 'shape-clover4',
    tone: 'bg-tertiary-container text-on-tertiary-container',
  },
]

const values = reactive({ ...fluxConfigStore.fluxBoost })
const ready = ref(false)
const readError = ref(false)
// One write at a time on this page: overlapping commits could restore a
// snapshot taken before the other one succeeded.
const saving = ref(false)

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
  } catch (error) {
    console.error('Failed to load Flux Boost settings:', error)
    readError.value = true
  }
  Object.assign(values, fluxConfigStore.fluxBoost)
  ready.value = true
})

async function toggle(key, enabled) {
  if (saving.value) return
  // Turning stable clocks off pins CPU, GPU and memory at their maximum all game.
  if (key === 'sustained_mode' && !enabled) {
    const ok = await notify.confirm({
      tone: 'warning',
      title: t('flux_boost.sustained.off_title'),
      message: t('flux_boost.sustained.off_message'),
      confirmText: t('flux_boost.sustained.off_action'),
    })
    if (!ok) return
  }
  saving.value = true
  values[key] = enabled
  try {
    await fluxConfigStore.commit(() => fluxConfigStore.setFluxBoost(key, enabled))
    readError.value = false
    const part = parts.find((p) => p.key === key)
    notify.success(
      t(enabled ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t(`flux_boost.${part.label}.title`),
      }),
    )
  } catch (error) {
    console.error(`Failed to set ${key}:`, error)
    notify.error(t('notify.save_failed'))
  } finally {
    values[key] = fluxConfigStore.fluxBoost[key]
    saving.value = false
  }
}
</script>
