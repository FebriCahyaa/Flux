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
        <h1 class="m3-headline text-4xl text-on-surface mt-8 mb-6 px-1">
          {{ $t('device_mitigation.title') }}
        </h1>

        <div class="m3-enter aspect-3/2 rounded-[32px] overflow-hidden mb-3">
          <img
            src="/illustration/device_mitigation_poster.avif"
            class="w-full h-full object-cover"
            alt=""
          />
        </div>

        <!-- Main switch: saved immediately -->
        <div
          class="m3-enter rounded-[28px] p-5 mb-6 flex items-center justify-between gap-4 transition-colors"
          :class="
            enabled
              ? 'bg-primary-container text-on-primary-container'
              : 'bg-surface-container-high text-on-surface'
          "
          style="animation-delay: 40ms"
        >
          <div class="min-w-0">
            <h2 class="text-base font-semibold">{{ $t('device_mitigation.toggle_title') }}</h2>
            <p class="text-xs mt-1 opacity-80">
              {{ enabled ? $t('device_mitigation.state_on') : $t('device_mitigation.state_off') }}
            </p>
          </div>
          <ToggleSwitch :model-value="enabled" @update:modelValue="toggle" />
        </div>

        <!-- What it changes -->
        <h2 class="text-sm font-semibold text-primary px-4 pb-2">
          {{ $t('device_mitigation.changes_title') }}
        </h2>
        <div class="mb-6">
          <div
            v-for="(item, i) in items"
            :key="item"
            class="md3-list m3-enter"
            :style="{ animationDelay: `${80 + i * 50}ms` }"
          >
            <div class="md3-list-item flex items-start gap-4 px-5 py-4 cursor-default">
              <span class="item-badge mt-0.5" :class="[badge(i).shape, badge(i).tone]">
                <component :is="badge(i).icon" />
              </span>
              <div class="flex-1 min-w-0">
                <div class="flex items-center justify-between gap-2">
                  <h3 class="text-sm font-semibold text-on-surface">{{ itemTitle(item) }}</h3>
                  <span
                    class="shrink-0 rounded-full px-2.5 py-0.5 text-[11px] font-semibold transition-colors"
                    :class="
                      enabled
                        ? 'bg-primary text-on-primary'
                        : 'bg-surface-container-highest text-on-surface-variant'
                    "
                  >
                    {{
                      enabled
                        ? $t('device_mitigation.applied')
                        : $t('device_mitigation.not_applied')
                    }}
                  </span>
                </div>
                <p class="text-xs text-on-surface-variant mt-1 leading-relaxed">
                  {{ itemDescription(item) }}
                </p>
                <code
                  class="allow-copy block text-[10px] text-on-surface-variant/70 mt-2 tracking-wide"
                  >{{ item }}</code
                >
              </div>
            </div>
          </div>
        </div>

        <!-- Chipset rules from device_mitigation.json that match this device -->
        <h2 class="text-sm font-semibold text-primary px-4 pb-2">
          {{ $t('device_mitigation.rules_title') }}
        </h2>
        <div class="mb-6">
          <div v-for="rule in matchedRules" :key="rule.id" class="md3-list m3-enter">
            <div class="md3-list-item px-5 py-4 cursor-default">
              <div class="flex items-center justify-between gap-2">
                <h3 class="text-sm font-semibold text-on-surface">{{ rule.name }}</h3>
                <span
                  class="shrink-0 rounded-full px-2.5 py-0.5 text-[11px] font-semibold bg-primary text-on-primary"
                  >{{ $t('device_mitigation.applied') }}</span
                >
              </div>
              <p class="text-xs text-on-surface-variant mt-1 leading-relaxed">
                {{ rule.description }}
              </p>
              <span class="flex flex-wrap gap-1 mt-2">
                <span
                  v-for="item in rule.items"
                  :key="item"
                  class="rounded-full px-2.5 py-0.5 text-[11px] font-semibold bg-surface-container-highest text-on-surface"
                  >{{ itemTitle(item) }}</span
                >
              </span>
            </div>
          </div>
          <p v-if="!matchedRules.length" class="text-xs text-on-surface-variant px-4">
            {{ $t('device_mitigation.rules_none') }}
          </p>
          <p v-else class="text-xs text-on-surface-variant px-4 mt-2">
            {{ $t('device_mitigation.rules_note') }}
          </p>
        </div>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('device_mitigation.brief') }} {{ $t('device_mitigation.apply_note') }}
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
import { exec } from 'kernelsu'
import * as KernelSU from '@/helpers/KernelSU'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

// Rules shipped with the module; "default.items" is what this switch turns on.
const RULES_FILE = '/data/adb/.config/flux/device_mitigation.json'
const FALLBACK_ITEMS = ['DISABLE_DDR_TWEAK', 'NO_PERFORMANCE_CPUGOV', 'QCOM_NO_GPU_POWERSAVE']

// Same matching as fluxd (DeviceMitigationStore): soc = /proc/device-tree/model,
// model = ro.product.model, uname = kernel release; operators match / contains / regex.
function ruleMatches(rule, info) {
  const conds = Object.entries(rule?.filter_condition || {})
  if (!conds.length) return false
  const results = conds.map(([name, cond]) => {
    const value = info[name]
    if (value === undefined) return false
    if (cond.operator === 'match') return value === cond.value
    if (cond.operator === 'contains') return value.includes(cond.value)
    if (cond.operator === 'regex') {
      try {
        return new RegExp(cond.value).test(value)
      } catch {
        return false
      }
    }
    return false
  })
  return rule.filter_type === 'all' ? results.every(Boolean) : results.some(Boolean)
}

const router = useRouter()
const { t, te } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const enabled = ref(false)
const items = ref(FALLBACK_ITEMS)
const matchedRules = ref([])

const badges = [
  {
    icon: ChipsetIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    icon: TuneIcon,
    shape: 'shape-pentagon',
    tone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    icon: BatterySaverIcon,
    shape: 'shape-clover4',
    tone: 'bg-tertiary-container text-on-tertiary-container',
  },
]
const badge = (i) => badges[i % badges.length]

const itemTitle = (item) =>
  te(`device_mitigation.items.${item}.title`) ? t(`device_mitigation.items.${item}.title`) : item
const itemDescription = (item) =>
  te(`device_mitigation.items.${item}.description`)
    ? t(`device_mitigation.items.${item}.description`)
    : ''

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    enabled.value = fluxConfigStore.isDeviceMitigationEnabled
  } catch (error) {
    console.error('Failed to load device mitigation setting:', error)
  }

  try {
    const rules = JSON.parse(await KernelSU.readFile(RULES_FILE))
    const list = rules?.default?.items
    if (Array.isArray(list) && list.length) items.value = list.filter((i) => typeof i === 'string')

    const { stdout } = await exec(
      'tr -d "\\000" </proc/device-tree/model; echo; getprop ro.product.model; uname -r',
    )
    const [soc = '', model = '', uname = ''] = stdout.split('\n').map((l) => l.trim())
    matchedRules.value = Object.entries(rules?.device_rules || {})
      .filter(([, rule]) => ruleMatches(rule, { soc, model, uname }))
      .map(([id, rule]) => ({
        id,
        name: rule.name || id,
        description: rule.description || '',
        items: (rule.items || []).filter((i) => typeof i === 'string'),
      }))
  } catch (error) {
    console.error('Failed to read device mitigation rules:', error)
  }
})

// Saved right away, like the other settings pages: closing the WebUI must not lose the change.
async function toggle(value) {
  enabled.value = value
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    fluxConfigStore.setDeviceMitigation(value)
    await fluxConfigStore.saveConfig()
    notify.success(
      t(value ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t('settings_page.device_mitigation.title'),
      }),
    )
  } catch (error) {
    console.error('Failed to set device mitigation:', error)
    enabled.value = fluxConfigStore.isDeviceMitigationEnabled
    notify.error(t('notify.save_failed'))
  }
}

function goBack() {
  router.back()
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
