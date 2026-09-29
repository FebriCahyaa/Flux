<template>
  <SettingsDetailLayout
    :title="$t('disable_tweaks.title')"
    :description="$t('disable_tweaks.brief')"
    :icon="PauseIcon"
    shape="shape-burst"
    :tone="
      enabled
        ? 'bg-error-container text-on-error-container'
        : 'bg-primary-container text-on-primary-container'
    "
    :read-error="readError"
  >
    <!-- Inverted switch: ON means Flux's tweaks are OFF, so "on" uses the
         error tone and the state line spells out what that means. -->
    <SettingsSwitch
      variant="main"
      tone="error"
      :title="$t('disable_tweaks.toggle_title')"
      :description="enabled ? $t('disable_tweaks.state_on') : $t('disable_tweaks.state_off')"
      :model-value="enabled"
      :busy="saving"
      :disabled="!ready"
      @update:model-value="toggle"
    />

    <template v-for="group in groups" :key="group.key">
      <h2 class="text-sm font-semibold px-4 pb-2" :class="group.titleTone">
        {{ $t(`disable_tweaks.${group.key}_title`) }}
      </h2>
      <ul class="mb-6">
        <li v-for="item in group.items" :key="item.key" class="md3-list">
          <div class="md3-list-item flex items-center gap-4 px-5 py-3.5 cursor-default">
            <span class="item-badge" :class="[item.shape, group.badgeTone]" aria-hidden="true">
              <component :is="item.icon" :size="20" />
            </span>
            <span class="flex-1 text-sm text-on-surface">{{
              $t(`disable_tweaks.items.${item.key}`)
            }}</span>
            <!-- The group heading already says stop/keep; the mark is visual only. -->
            <component
              :is="group.key === 'stops' ? CloseIcon : CheckIcon"
              :size="18"
              aria-hidden="true"
              :class="group.key === 'stops' && enabled ? 'text-error' : 'text-on-surface-variant'"
            />
          </div>
        </li>
      </ul>
    </template>

    <SettingsNote>{{ $t('disable_tweaks.reboot_note') }}</SettingsNote>
  </SettingsDetailLayout>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { exec } from 'kernelsu'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsSwitch from '@/components/ui/SettingsSwitch.vue'
import SettingsNote from '@/components/ui/SettingsNote.vue'
import PauseIcon from '@/components/icons/Pause.vue'
import CloseIcon from '@/components/icons/Close.vue'
import CheckIcon from '@/components/icons/CheckCircle.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import WifiIcon from '@/components/icons/Wifi.vue'
import GpuIcon from '@/components/icons/Gpu.vue'
import GamesIcon from '@/components/icons/Games.vue'
import MonitorIcon from '@/components/icons/Monitor.vue'
import AppWindowIcon from '@/components/icons/AppWindow.vue'

const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const enabled = ref(false)
const ready = ref(false)
const readError = ref(false)
const saving = ref(false)

const groups = [
  {
    key: 'stops',
    titleTone: 'text-error',
    badgeTone: 'bg-error-container text-on-error-container',
    items: [
      { key: 'profiles', icon: ChipsetIcon, shape: 'shape-cookie9' },
      { key: 'boost', icon: BoltChargeIcon, shape: 'shape-pentagon' },
      { key: 'game_tweaks', icon: WifiIcon, shape: 'shape-clover4' },
      { key: 'governors', icon: GpuIcon, shape: 'shape-cookie6' },
    ],
  },
  {
    key: 'keeps',
    titleTone: 'text-primary',
    badgeTone: 'bg-primary-container text-on-primary-container',
    items: [
      { key: 'detection', icon: GamesIcon, shape: 'shape-cookie9' },
      { key: 'monitor', icon: MonitorIcon, shape: 'shape-flower' },
      { key: 'addons', icon: AppWindowIcon, shape: 'shape-sunny' },
    ],
  },
]

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    enabled.value = fluxConfigStore.isDisableTweaksEnabled
  } catch (error) {
    console.error('Failed to load disable tweaks setting:', error)
    readError.value = true
  }
  ready.value = true
})

async function toggle(value) {
  if (saving.value) return
  if (value) {
    const ok = await notify.confirm({
      tone: 'danger',
      title: t('disable_tweaks.confirm.title'),
      message: t('disable_tweaks.confirm.message'),
      points: [
        t('disable_tweaks.items.profiles'),
        t('disable_tweaks.items.boost'),
        t('disable_tweaks.items.game_tweaks'),
      ],
      confirmText: t('disable_tweaks.confirm.action'),
    })
    if (!ok) return
  }

  saving.value = true
  enabled.value = value
  try {
    await fluxConfigStore.commit(() => fluxConfigStore.setDisableTweaks(value))
    readError.value = false
  } catch (error) {
    console.error('Failed to set disable tweaks:', error)
    notify.error(t('notify.save_failed'))
    return
  } finally {
    enabled.value = fluxConfigStore.isDisableTweaksEnabled
    saving.value = false
  }

  const reboot = await notify.confirm({
    tone: 'info',
    title: t('reboot_modal.title'),
    message: t('reboot_modal.description'),
    confirmText: t('reboot_modal.reboot'),
    cancelText: t('reboot_modal.later'),
  })
  if (reboot) {
    exec('reboot').catch((error) => console.error('Failed to reboot device:', error))
  } else {
    notify.show(t('disable_tweaks.saved_reboot_later'))
  }
}
</script>

<style scoped>
.item-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
</style>
