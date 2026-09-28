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
        <div class="flex items-center gap-4 mt-8 mb-6 px-1">
          <span class="hero-badge shape-burst bg-tertiary-container text-on-tertiary-container">
            <BoltChargeIcon />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('perf_boost.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-6">
          {{ $t('perf_boost.brief') }}
        </p>

        <!-- Toggle -->
        <div class="md3-list mb-6">
          <div class="md3-list-item flex items-center gap-4 px-5 py-4">
            <span class="item-badge shape-burst bg-tertiary-container text-on-tertiary-container">
              <BoltChargeIcon />
            </span>
            <span class="flex-1 min-w-0">
              <span class="block text-sm font-semibold text-on-surface">
                {{ $t('perf_boost.toggle_title') }}
              </span>
              <span class="block text-xs text-on-surface-variant mt-1 leading-relaxed">
                {{ enabled ? $t('perf_boost.state_on') : $t('perf_boost.state_off') }}
              </span>
            </span>
            <ToggleSwitch
              id="perf-boost-toggle"
              :model-value="enabled"
              @update:modelValue="toggle"
            />
          </div>
        </div>

        <!-- What it changes -->
        <h2 class="text-sm font-semibold text-primary px-4 pb-2">
          {{ $t('perf_boost.effects_title') }}
        </h2>
        <div class="mb-6">
          <div
            v-for="(item, i) in effectItems"
            :key="item.key"
            class="md3-list m3-enter"
            :style="{ animationDelay: `${i * 40}ms` }"
          >
            <div class="md3-list-item flex items-start gap-4 px-5 py-4">
              <span class="item-badge mt-0.5" :class="[item.shape, item.tone]">
                <component :is="item.icon" :size="20" />
              </span>
              <span class="flex-1 min-w-0">
                <span class="block text-sm font-semibold text-on-surface">
                  {{ $t(`perf_boost.effects.${item.key}.title`) }}
                </span>
                <span class="block text-xs text-on-surface-variant mt-1 leading-relaxed">
                  {{ $t(`perf_boost.effects.${item.key}.description`) }}
                </span>
              </span>
            </div>
          </div>
        </div>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('perf_boost.apply_note') }}
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
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import SpeedIcon from '@/components/icons/Speed.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import StarIcon from '@/components/icons/Star.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const enabled = ref(true)

const effectItems = [
  {
    key: 'migration',
    icon: TuneIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'latency',
    icon: SpeedIcon,
    shape: 'shape-sunny',
    tone: 'bg-tertiary-container text-on-tertiary-container',
  },
  {
    key: 'autogroup',
    icon: ChipsetIcon,
    shape: 'shape-clover4',
    tone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'bandwidth',
    icon: StarIcon,
    shape: 'shape-pentagon',
    tone: 'bg-tertiary-container text-on-tertiary-container',
  },
]

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    enabled.value = fluxConfigStore.perfBoost
  } catch {
    // keep default
  }
})

async function toggle(val) {
  enabled.value = val
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setPerfBoost(val)
    await fluxConfigStore.saveConfig()
    notify.success(
      t(val ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t('perf_boost.title'),
      }),
    )
  } catch {
    enabled.value = fluxConfigStore.perfBoost
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

.item-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
</style>
