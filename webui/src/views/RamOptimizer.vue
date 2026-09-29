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
          <span class="hero-badge shape-cookie12 bg-secondary-container text-on-secondary-container">
            <MemoryIcon />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('ram_optimizer.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-6">
          {{ $t('ram_optimizer.brief') }}
        </p>

        <!-- RAM tier card -->
        <div v-if="ramTier" class="tier-card mb-5">
          <span class="tier-badge shape-cookie6 bg-secondary-container text-on-secondary-container">
            <ChipsetIcon :size="22" />
          </span>
          <div class="flex-1 min-w-0">
            <p class="text-sm font-semibold text-on-surface">
              {{ $t('ram_optimizer.tier_title', { tier: ramTier }) }}
            </p>
            <p class="text-xs text-on-surface-variant mt-1 leading-relaxed">
              {{ $t('ram_optimizer.tier_description') }}
            </p>
          </div>
        </div>

        <!-- Toggle -->
        <div class="md3-list mb-6">
          <div class="md3-list-item flex items-center gap-4 px-5 py-4">
            <span class="item-badge shape-clover4 bg-secondary-container text-on-secondary-container">
              <MemoryIcon />
            </span>
            <span class="flex-1 min-w-0">
              <span class="block text-sm font-semibold text-on-surface">
                {{ $t('ram_optimizer.toggle_title') }}
              </span>
              <span class="block text-xs text-on-surface-variant mt-1 leading-relaxed">
                {{ enabled ? $t('ram_optimizer.state_on') : $t('ram_optimizer.state_off') }}
              </span>
            </span>
            <ToggleSwitch
              id="ram-optimizer-toggle"
              :model-value="enabled"
              @update:modelValue="toggle"
            />
          </div>
        </div>

        <!-- What it tunes -->
        <h2 class="text-sm font-semibold text-primary px-4 pb-2">
          {{ $t('ram_optimizer.tunes_title') }}
        </h2>
        <div class="mb-6">
          <div
            v-for="(item, i) in tuneItems"
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
                  {{ $t(`ram_optimizer.tunes.${item.key}.title`) }}
                </span>
                <span class="block text-xs text-on-surface-variant mt-1 leading-relaxed">
                  {{ $t(`ram_optimizer.tunes.${item.key}.description`) }}
                </span>
              </span>
            </div>
          </div>
        </div>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('ram_optimizer.apply_note') }}
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
import MemoryIcon from '@/components/icons/Memory.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import SpeedIcon from '@/components/icons/Speed.vue'
import SparkleIcon from '@/components/icons/Sparkle.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const enabled = ref(true)
const ramTier = ref(null)

const tuneItems = [
  {
    key: 'swappiness',
    icon: TuneIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'watermark',
    icon: SpeedIcon,
    shape: 'shape-sunny',
    tone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'vfs_cache',
    icon: SparkleIcon,
    shape: 'shape-pentagon',
    tone: 'bg-tertiary-container text-on-tertiary-container',
  },
  {
    key: 'writeback',
    icon: BoltChargeIcon,
    shape: 'shape-clover4',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'compaction',
    icon: TuneIcon,
    shape: 'shape-cookie6',
    tone: 'bg-secondary-container text-on-secondary-container',
  },
]

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    enabled.value = fluxConfigStore.ramOptimizer
  } catch {
    // keep default
  }

  // Detect RAM tier from /proc/meminfo
  try {
    const { stdout } = await exec(
      "awk '/^MemTotal:/{print $2;exit}' /proc/meminfo",
    )
    const kb = parseInt(stdout.trim(), 10)
    if (!isNaN(kb)) {
      const mb = Math.floor(kb / 1024)
      if (mb <= 3584) ramTier.value = 3
      else if (mb <= 4608) ramTier.value = 4
      else if (mb <= 6656) ramTier.value = 6
      else if (mb <= 8704) ramTier.value = 8
      else if (mb <= 12800) ramTier.value = 12
      else ramTier.value = 16
    }
  } catch {
    // no tier shown
  }
})

async function toggle(val) {
  enabled.value = val
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setRamOptimizer(val)
    await fluxConfigStore.saveConfig()
    notify.success(
      t(val ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t('ram_optimizer.title'),
      }),
    )
  } catch {
    enabled.value = fluxConfigStore.ramOptimizer
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

.tier-card {
  display: flex;
  gap: 14px;
  align-items: flex-start;
  padding: 16px 18px;
  border-radius: 24px;
  background: var(--color-surface-container);
}

.tier-badge {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
</style>
