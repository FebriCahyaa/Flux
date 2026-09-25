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
          <span class="hero-badge shape-burst bg-primary-container text-on-primary-container">
            <BoltChargeIcon />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('flux_boost.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-6">
          {{ $t('flux_boost.brief') }}
        </p>

        <div class="mb-6">
          <div
            v-for="(part, i) in parts"
            :key="part.key"
            class="md3-list m3-enter"
            :style="{ animationDelay: `${i * 50}ms` }"
          >
            <div class="md3-list-item flex items-center gap-4 px-5 py-4">
              <span class="part-badge" :class="[part.shape, part.tone]">
                <component :is="part.icon" />
              </span>
              <span class="flex-1 min-w-0">
                <span class="block text-sm font-semibold text-on-surface">{{
                  $t(`flux_boost.${part.label}.title`)
                }}</span>
                <span class="block text-xs text-on-surface-variant mt-1">{{
                  $t(`flux_boost.${part.label}.description`)
                }}</span>
              </span>
              <ToggleSwitch
                :id="`boost-${part.key}`"
                :model-value="values[part.key]"
                @update:modelValue="(v) => toggle(part.key, v)"
              />
            </div>
          </div>
        </div>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('flux_boost.apply_note') }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useFluxConfigStore } from '@/stores/FluxConfig'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import StarIcon from '@/components/icons/Star.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

const router = useRouter()
const fluxConfigStore = useFluxConfigStore()

// Config keys match FluxConfigStore::Preferences in fluxd.
const parts = [
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

const values = reactive({ flux_vm: true, flux_io: true, game_priority: true })

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    Object.assign(values, fluxConfigStore.fluxBoost)
  } catch (error) {
    console.error('Failed to load Flux Boost settings:', error)
  }
})

async function toggle(key, enabled) {
  values[key] = enabled
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setFluxBoost(key, enabled)
    await fluxConfigStore.saveConfig()
  } catch (error) {
    console.error(`Failed to set ${key}:`, error)
    values[key] = fluxConfigStore.fluxBoost[key]
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

.part-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
</style>
