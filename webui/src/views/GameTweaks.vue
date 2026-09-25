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
            <GamesIcon :size="28" />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('game_tweaks.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-4">
          {{ $t('game_tweaks.brief') }}
        </p>

        <!-- Kernel the tweaks adapt to -->
        <div v-if="caps" class="kernel-card mb-5">
          <span class="kernel-badge shape-cookie6" :class="kernelTone">
            <ChipsetIcon :size="22" />
          </span>
          <div class="flex-1 min-w-0">
            <p class="text-sm font-semibold text-on-surface">
              {{ $t(`game_tweaks.kernel.${kernelType}.title`) }}
            </p>
            <p class="text-xs text-on-surface-variant mt-0.5 truncate font-mono">
              {{ caps.kernel }}
            </p>
            <p class="text-xs text-on-surface-variant mt-1.5 leading-relaxed">
              {{ $t(`game_tweaks.kernel.${kernelType}.description`) }}
            </p>
          </div>
        </div>

        <div class="mb-6">
          <div
            v-for="(item, i) in visibleItems"
            :key="item.key"
            class="md3-list m3-enter"
            :style="{ animationDelay: `${i * 50}ms` }"
          >
            <div class="md3-list-item flex items-center gap-4 px-5 py-4">
              <span class="item-badge" :class="[item.shape, item.tone]">
                <component :is="item.icon" :size="22" />
              </span>
              <span class="flex-1 min-w-0">
                <span class="flex flex-wrap items-center gap-x-2 gap-y-1">
                  <span class="text-sm font-semibold text-on-surface">{{
                    $t(`game_tweaks.${item.key}.title`)
                  }}</span>
                  <span v-if="item.tag" class="tag" :class="item.tagTone">{{
                    $t(`game_tweaks.tags.${item.tag}`)
                  }}</span>
                </span>
                <span class="block text-xs text-on-surface-variant mt-1 leading-relaxed">{{
                  $t(`game_tweaks.${item.key}.description`)
                }}</span>
                <span
                  v-if="item.key === 'chipset_boost' && chipsetParts.length"
                  class="flex flex-wrap gap-1 mt-2"
                >
                  <span
                    v-for="part in chipsetParts"
                    :key="part"
                    class="tag bg-surface-container-highest text-on-surface"
                    >{{ $t(`game_tweaks.chipset_boost.parts.${part}`) }}</span
                  >
                </span>
              </span>
              <ToggleSwitch
                :id="`tweak-${item.key}`"
                :model-value="values[item.key]"
                @update:modelValue="(v) => toggle(item, v)"
              />
            </div>
          </div>
        </div>

        <!-- Tweaks this device cannot use are hidden -->
        <div v-if="hiddenItems.length" class="hidden-card mb-4">
          <EyeOffIcon class="shrink-0 text-on-surface-variant" :size="20" />
          <div class="flex-1 min-w-0">
            <p class="text-sm font-semibold text-on-surface">
              {{ $t('game_tweaks.hidden_title', { n: hiddenItems.length }) }}
            </p>
            <p class="text-xs text-on-surface-variant mt-1 leading-relaxed">
              {{ hiddenItems.map((i) => $t(`game_tweaks.${i.key}.title`)).join(' · ') }}
            </p>
          </div>
        </div>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('game_tweaks.apply_note') }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { reactive, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import { useCapabilitiesStore } from '@/stores/Capabilities'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import GamesIcon from '@/components/icons/Games.vue'
import WifiIcon from '@/components/icons/Wifi.vue'
import TouchTapIcon from '@/components/icons/TouchTap.vue'
import SpeedIcon from '@/components/icons/Speed.vue'
import SparkleIcon from '@/components/icons/Sparkle.vue'
import LayersIcon from '@/components/icons/Layers.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import EyeOffIcon from '@/components/icons/EyeOff.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()
const capabilities = useCapabilitiesStore()

// Keys match FluxConfigStore::Preferences in fluxd. `confirmOn` asks before enabling;
// `cap` names the capabilities (flux_utility capabilities) the tweak needs.
const CHIPSET_PARTS = ['core_ctl', 'sched_boost', 'kgsl', 'mali']

const items = [
  {
    key: 'net_tweaks',
    cap: 'net',
    icon: WifiIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'touch_tweaks',
    cap: 'touchpanel',
    icon: TouchTapIcon,
    shape: 'shape-flower',
    tone: 'bg-secondary-container text-on-secondary-container',
    tag: 'oplus',
    tagTone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'game_refresh_rate',
    cap: 'refresh',
    icon: SpeedIcon,
    shape: 'shape-sunny',
    tone: 'bg-tertiary-container text-on-tertiary-container',
    tag: 'battery',
    tagTone: 'bg-tertiary-container text-on-tertiary-container',
    confirmOn: true,
  },
  {
    key: 'surface_boost',
    cap: 'surface',
    icon: LayersIcon,
    shape: 'shape-clover4',
    tone: 'bg-primary-container text-on-primary-container',
    tag: 'all_devices',
    tagTone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'chipset_boost',
    cap: CHIPSET_PARTS,
    icon: ChipsetIcon,
    shape: 'shape-burst',
    tone: 'bg-tertiary-container text-on-tertiary-container',
    tag: 'not_lite',
    tagTone: 'bg-tertiary-container text-on-tertiary-container',
  },
  {
    key: 'drop_caches',
    icon: SparkleIcon,
    shape: 'shape-pentagon',
    tone: 'bg-surface-container-highest text-on-surface',
  },
]

const values = reactive({ ...fluxConfigStore.gameTweaks })

const caps = computed(() => capabilities.caps)
const visibleItems = computed(() => items.filter((i) => capabilities.supports(i.cap)))
const hiddenItems = computed(() => items.filter((i) => !capabilities.supports(i.cap)))
const chipsetParts = computed(() => CHIPSET_PARTS.filter((p) => caps.value?.[p]))
const kernelType = computed(() =>
  ['gki', 'non_gki', 'legacy'].includes(caps.value?.kernel_type)
    ? caps.value.kernel_type
    : 'unknown',
)
const kernelTone = computed(
  () =>
    ({
      gki: 'bg-primary-container text-on-primary-container',
      non_gki: 'bg-secondary-container text-on-secondary-container',
      legacy: 'bg-tertiary-container text-on-tertiary-container',
    })[kernelType.value] || 'bg-surface-container-highest text-on-surface',
)

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    Object.assign(values, fluxConfigStore.gameTweaks)
  } catch (error) {
    console.error('Failed to load game tweaks:', error)
  }
  capabilities.load()
})

async function toggle(item, enabled) {
  if (enabled && item.confirmOn) {
    const ok = await notify.confirm({
      tone: 'warning',
      title: t(`game_tweaks.${item.key}.confirm_title`),
      message: t(`game_tweaks.${item.key}.confirm_message`),
      confirmText: t('common.enable'),
    })
    if (!ok) return
  }

  values[item.key] = enabled
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setGameTweak(item.key, enabled)
    await fluxConfigStore.saveConfig()
    notify.success(
      t(enabled ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t(`game_tweaks.${item.key}.title`),
      }),
    )
  } catch (error) {
    console.error(`Failed to set ${item.key}:`, error)
    values[item.key] = fluxConfigStore.gameTweaks[item.key]
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

.kernel-card,
.hidden-card {
  display: flex;
  gap: 14px;
  align-items: flex-start;
  padding: 16px 18px;
  border-radius: 24px;
  background: var(--color-surface-container);
}

.hidden-card {
  background: transparent;
  border: 1px dashed var(--color-outline-variant);
}

.kernel-badge {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.tag {
  font-size: 10px;
  font-weight: 700;
  padding: 2px 8px;
  border-radius: 999px;
}
</style>
