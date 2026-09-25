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
          <span class="hero-badge shape-cookie12 bg-tertiary-container text-on-tertiary-container">
            <ChipsetIcon :size="28" />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('cpu_governor.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-5">
          {{ $t('cpu_governor.brief') }}
        </p>

        <div class="notice mb-4">
          <WarningIcon class="shrink-0 text-on-tertiary-container" :size="20" />
          <p class="text-xs leading-relaxed">{{ $t('cpu_governor.notice') }}</p>
        </div>

        <div class="space-y-3 mb-5">
          <GovernorPicker
            class="m3-enter"
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
            class="m3-enter"
            style="animation-delay: 60ms"
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

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('cpu_governor.apply_note') }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import * as KernelSU from '@/helpers/KernelSU'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import WarningIcon from '@/components/icons/Warning.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import GovernorPicker from '@/components/ui/GovernorPicker.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const available = ref([])
const options = computed(() => available.value.map((g) => ({ value: g, label: g })))
const balanceGovernor = computed(() => fluxConfigStore.balanceGovernor)
const powersaveGovernor = computed(() => fluxConfigStore.powersaveGovernor)

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
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

  try {
    if (profile === 'balance') fluxConfigStore.setBalanceGovernor(governor)
    else fluxConfigStore.setPowersaveGovernor(governor)
    await fluxConfigStore.saveConfig()
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
}

.notice {
  display: flex;
  gap: 12px;
  align-items: flex-start;
  padding: 14px 16px;
  border-radius: 20px;
  background: var(--color-tertiary-container);
  color: var(--color-on-tertiary-container);
}
</style>
