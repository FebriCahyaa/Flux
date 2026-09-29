<template>
  <SettingsDetailLayout
    :title="$t('game_tweaks.title')"
    :description="$t('game_tweaks.brief')"
    :icon="GamesIcon"
    shape="shape-cookie12"
    tone="bg-tertiary-container text-on-tertiary-container"
    :read-error="readError"
  >
        <!-- Kernel the tweaks adapt to -->
        <div v-if="caps" class="kernel-card mb-5">
          <span class="kernel-badge shape-cookie6" :class="kernelTone" aria-hidden="true">
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

        <!-- Grouped by when the tweak applies (while you play / render & GPU
             while gaming / system, at boot), which is how fluxd applies them. -->
        <template v-for="group in GROUPS" :key="group">
          <h2 v-if="visibleIn(group).length" class="text-sm font-semibold text-primary px-4 pb-2">
            {{ $t(`game_tweaks.groups.${group}`) }}
          </h2>
          <div v-if="visibleIn(group).length" class="mb-6">
            <SettingsSwitch
              v-for="item in visibleIn(group)"
              :key="item.key"
              :title="$t(`game_tweaks.${item.key}.title`)"
              :description="$t(`game_tweaks.${item.key}.description`)"
              :icon="item.icon"
              :shape="item.shape"
              :badge-tone="item.tone"
              :model-value="values[item.key]"
              :busy="saving"
              :disabled="!ready"
              @update:model-value="(v) => toggle(item, v)"
            >
              <!-- Impact/requirement tag stays visible next to the title. -->
              <template v-if="item.tag" #tag>
                <span class="tag" :class="item.tagTone">{{ $t(`game_tweaks.tags.${item.tag}`) }}</span>
              </template>
              <span v-if="partsOf(item.key).length" class="flex flex-wrap gap-1 mt-2">
                <span
                  v-for="part in partsOf(item.key)"
                  :key="part"
                  class="tag bg-surface-container-highest text-on-surface"
                  >{{ $t(`game_tweaks.parts.${part}`) }}</span
                >
              </span>
            </SettingsSwitch>
          </div>
        </template>

        <!-- Tweaks this device cannot use are hidden -->
        <div v-if="hiddenItems.length" class="hidden-card mb-4">
          <EyeOffIcon class="shrink-0 text-on-surface-variant" :size="20" aria-hidden="true" />
          <div class="flex-1 min-w-0">
            <p class="text-sm font-semibold text-on-surface">
              {{ $t('game_tweaks.hidden_title', { n: hiddenItems.length }) }}
            </p>
            <p class="text-xs text-on-surface-variant mt-1 leading-relaxed">
              {{ hiddenItems.map((i) => $t(`game_tweaks.${i.key}.title`)).join(' · ') }}
            </p>
          </div>
        </div>

        <SettingsNote>{{ $t('game_tweaks.apply_note') }}</SettingsNote>
  </SettingsDetailLayout>
</template>

<script setup>
import { reactive, ref, computed, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import { useCapabilitiesStore } from '@/stores/Capabilities'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsSwitch from '@/components/ui/SettingsSwitch.vue'
import SettingsNote from '@/components/ui/SettingsNote.vue'
import GamesIcon from '@/components/icons/Games.vue'
import WifiIcon from '@/components/icons/Wifi.vue'
import TouchTapIcon from '@/components/icons/TouchTap.vue'
import SpeedIcon from '@/components/icons/Speed.vue'
import SparkleIcon from '@/components/icons/Sparkle.vue'
import LayersIcon from '@/components/icons/Layers.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import EyeOffIcon from '@/components/icons/EyeOff.vue'
import GpuIcon from '@/components/icons/Gpu.vue'
import MonitorIcon from '@/components/icons/Monitor.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'

const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()
const capabilities = useCapabilitiesStore()

// Keys match FluxConfigStore::Preferences in fluxd. `confirmOn` asks before enabling;
// `cap` names the capabilities (flux_utility capabilities) the tweak needs.
const CHIPSET_PARTS = ['core_ctl', 'sched_boost', 'kgsl', 'mali', 'workqueue']
const TOUCH_PARTS = ['input', 'touchpanel', 'sec_touch']

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
    cap: TOUCH_PARTS,
    icon: TouchTapIcon,
    shape: 'shape-flower',
    tone: 'bg-secondary-container text-on-secondary-container',
    tag: 'all_devices',
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
  // Render & GPU (fluxd RenderBooster, kgsl while gaming)
  {
    key: 'render_boost',
    group: 'render',
    icon: SpeedIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
    tag: 'all_devices',
    tagTone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'render_realtime',
    group: 'render',
    cap: 'clusters',
    icon: BoltChargeIcon,
    shape: 'shape-burst',
    tone: 'bg-tertiary-container text-on-tertiary-container',
    tag: 'advanced',
    tagTone: 'bg-tertiary-container text-on-tertiary-container',
    confirmOn: true,
  },
  {
    key: 'gpu_power_lock',
    group: 'render',
    cap: 'kgsl',
    icon: GpuIcon,
    shape: 'shape-sunny',
    tone: 'bg-secondary-container text-on-secondary-container',
    tag: 'heat',
    tagTone: 'bg-tertiary-container text-on-tertiary-container',
    confirmOn: true,
  },
  {
    key: 'adreno_reflex',
    group: 'render',
    cap: 'kgsl',
    icon: TouchTapIcon,
    shape: 'shape-flower',
    tone: 'bg-primary-container text-on-primary-container',
    tag: 'reboot',
    tagTone: 'bg-secondary-container text-on-secondary-container',
    confirmOn: true,
  },
  // System (flux_profiler system: at boot and when switched)
  {
    key: 'graphics_tweaks',
    group: 'system',
    icon: LayersIcon,
    shape: 'shape-clover4',
    tone: 'bg-secondary-container text-on-secondary-container',
    tag: 'reboot',
    tagTone: 'bg-secondary-container text-on-secondary-container',
    confirmOn: true,
  },
  {
    key: 'adaptive_refresh',
    group: 'system',
    cap: 'refresh',
    icon: MonitorIcon,
    shape: 'shape-pentagon',
    tone: 'bg-tertiary-container text-on-tertiary-container',
    tag: 'reboot',
    tagTone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'zram_tune',
    group: 'system',
    cap: 'zram',
    icon: ChipsetIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
]
const GROUPS = ['game', 'render', 'system']

const values = reactive({ ...fluxConfigStore.gameTweaks })
const ready = ref(false)
const readError = ref(false)
// One write at a time: overlapping commits could restore a stale snapshot.
const saving = ref(false)

const caps = computed(() => capabilities.caps)
const visibleItems = computed(() => items.filter((i) => capabilities.supports(i.cap)))
const visibleIn = (group) => visibleItems.value.filter((i) => (i.group || 'game') === group)
const hiddenItems = computed(() => items.filter((i) => !capabilities.supports(i.cap)))
const detected = (list) => list.filter((p) => caps.value?.[p])
const partsOf = (key) =>
  ({ chipset_boost: detected(CHIPSET_PARTS), touch_tweaks: detected(TOUCH_PARTS) })[key] || []
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
  } catch (error) {
    console.error('Failed to load game tweaks:', error)
    readError.value = true
  }
  Object.assign(values, fluxConfigStore.gameTweaks)
  ready.value = true
  capabilities.load()
})

async function toggle(item, enabled) {
  if (saving.value) return
  if (enabled && item.confirmOn) {
    const ok = await notify.confirm({
      tone: 'warning',
      title: t(`game_tweaks.${item.key}.confirm_title`),
      message: t(`game_tweaks.${item.key}.confirm_message`),
      confirmText: t('common.enable'),
    })
    if (!ok) return
  }

  saving.value = true
  values[item.key] = enabled
  try {
    await fluxConfigStore.commit(() => fluxConfigStore.setGameTweak(item.key, enabled))
    readError.value = false
    notify.success(
      t(enabled ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t(`game_tweaks.${item.key}.title`),
      }),
    )
  } catch (error) {
    console.error(`Failed to set ${item.key}:`, error)
    notify.error(t('notify.save_failed'))
  } finally {
    values[item.key] = fluxConfigStore.gameTweaks[item.key]
    saving.value = false
  }
}
</script>

<style scoped>
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
