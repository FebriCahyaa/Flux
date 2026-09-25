<template>
  <div class="page h-full flex flex-col overflow-hidden bg-surface">
    <div class="max-w-3xl mx-auto h-full flex flex-col w-full">
      <div class="flex-none p-5 pb-3">
        <button
          @click="goBack"
          class="m3-press w-10 h-10 -ms-2 rounded-full grid place-items-center text-on-surface hover:bg-surface-container-high"
          :aria-label="$t('common.cancel')"
        >
          <ArrowLeftIcon class="w-6 h-6 rtl:rotate-180" />
        </button>
      </div>

      <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll px-4">
        <div class="flex items-center gap-4 mt-8 mb-4 px-1">
          <span
            class="hero-badge shape-burst"
            :class="
              enabled
                ? 'bg-error-container text-on-error-container'
                : 'bg-primary-container text-on-primary-container'
            "
          >
            <PauseIcon :size="28" />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('disable_tweaks.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-5">
          {{ $t('disable_tweaks.brief') }}
        </p>

        <!-- Main switch -->
        <div class="switch-card mb-6" :class="{ on: enabled }">
          <div class="flex-1 min-w-0">
            <h2 class="text-base font-semibold">{{ $t('disable_tweaks.toggle_title') }}</h2>
            <p class="text-xs mt-1 opacity-80">
              {{ enabled ? $t('disable_tweaks.state_on') : $t('disable_tweaks.state_off') }}
            </p>
          </div>
          <ToggleSwitch id="disable-tweaks" :model-value="enabled" @update:modelValue="toggle" />
        </div>

        <template v-for="group in groups" :key="group.key">
          <h2 class="text-sm font-semibold px-3 mb-2" :class="group.titleTone">
            {{ $t(`disable_tweaks.${group.key}_title`) }}
          </h2>
          <div class="mb-6">
            <div v-for="item in group.items" :key="item.key" class="md3-list">
              <div class="md3-list-item flex items-center gap-4 px-5 py-3.5">
                <span class="item-badge" :class="[item.shape, group.badgeTone]">
                  <component :is="item.icon" :size="20" />
                </span>
                <span class="flex-1 text-sm text-on-surface">{{
                  $t(`disable_tweaks.items.${item.key}`)
                }}</span>
                <component
                  :is="group.key === 'stops' ? CloseIcon : CheckIcon"
                  :size="18"
                  :class="
                    group.key === 'stops' && enabled ? 'text-error' : 'text-on-surface-variant'
                  "
                />
              </div>
            </div>
          </div>
        </template>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('disable_tweaks.reboot_note') }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { exec } from 'kernelsu'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
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
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const enabled = ref(false)

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
  }
})

async function toggle(value) {
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

  enabled.value = value
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setDisableTweaks(value)
    await fluxConfigStore.saveConfig()
  } catch (error) {
    console.error('Failed to set disable tweaks:', error)
    enabled.value = fluxConfigStore.isDisableTweaksEnabled
    notify.error(t('notify.save_failed'))
    return
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

function goBack() {
  router.back()
}
</script>

<style scoped>
.hero-badge {
  width: 56px;
  height: 56px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
  transition: background-color var(--m3-spring-default-effects-duration)
    var(--m3-spring-default-effects);
}

.item-badge {
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.switch-card {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 20px 20px 20px 24px;
  border-radius: 28px;
  background: var(--color-surface-container-high);
  color: var(--color-on-surface);
  transition:
    background-color var(--m3-spring-default-effects-duration) var(--m3-spring-default-effects),
    border-radius var(--m3-spring-default-spatial-duration) var(--m3-spring-default-spatial);
}

.switch-card.on {
  border-radius: 36px;
  background: var(--color-error-container);
  color: var(--color-on-error-container);
}
</style>
