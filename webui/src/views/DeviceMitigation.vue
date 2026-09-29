<template>
  <SettingsDetailLayout
    :title="$t('device_mitigation.title')"
    :description="$t('device_mitigation.brief')"
    :icon="ShieldIcon"
    shape="shape-clover4"
    tone="bg-primary-container text-on-primary-container"
    :read-error="readError"
  >
        <div class="illustration rounded-[28px] overflow-hidden mb-4" aria-hidden="true">
          <img
            src="/illustration/device_mitigation_poster.avif"
            class="w-full h-full object-cover"
            alt=""
          />
        </div>

        <!-- Main switch: saved immediately -->
        <SettingsSwitch
          variant="main"
          :title="$t('device_mitigation.toggle_title')"
          :description="enabled ? $t('device_mitigation.state_on') : $t('device_mitigation.state_off')"
          :model-value="enabled"
          :busy="saving"
          :disabled="!ready"
          @update:model-value="toggle"
        />

        <!-- What it changes -->
        <h2 class="text-sm font-semibold text-primary px-4 pb-2">
          {{ $t('device_mitigation.changes_title') }}
        </h2>
        <div class="mb-6">
          <div v-for="(item, i) in items" :key="item" class="md3-list">
            <div class="md3-list-item flex items-start gap-4 px-5 py-4 cursor-default">
              <span
                class="item-badge mt-0.5"
                :class="[badge(i).shape, badge(i).tone]"
                aria-hidden="true"
              >
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
                <!-- Internal flag name: secondary, kept for bug reports. -->
                <code
                  class="allow-copy block text-[10px] text-on-surface-variant/70 mt-2 tracking-wide break-all"
                  >{{ item }}</code
                >
              </div>
            </div>
          </div>
        </div>

        <!-- Chipset rules from device_mitigation.json that match this device -->
        <h2 class="text-sm font-semibold text-primary px-4 pb-1">
          {{ $t('device_mitigation.rules_title') }}
        </h2>
        <p
          v-if="rulesState === 'ok' && (device.model || device.soc)"
          class="allow-copy text-xs text-on-surface-variant px-4 pb-2 break-words"
        >
          {{ $t('device_mitigation.detected_as', { model: device.model || '–', soc: device.soc || '–' }) }}
        </p>
        <div class="mb-6">
          <LoadingSpinner v-if="rulesState === 'loading'" class="py-2" :size="32" />
          <SettingsNote v-else-if="rulesState === 'error'" tone="warning">
            {{ $t('device_mitigation.rules_read_failed') }}
          </SettingsNote>
          <div v-for="rule in matchedRules" :key="rule.id" class="md3-list">
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
          <!-- "No rule matches" is only claimed once the rules were actually read. -->
          <p v-if="rulesState === 'ok' && !matchedRules.length" class="text-xs text-on-surface-variant px-4">
            {{ $t('device_mitigation.rules_none') }}
          </p>
          <p v-else-if="rulesState === 'ok'" class="text-xs text-on-surface-variant px-4 mt-2">
            {{ $t('device_mitigation.rules_note') }}
          </p>
        </div>

        <SettingsNote>{{ $t('device_mitigation.apply_note') }}</SettingsNote>
  </SettingsDetailLayout>
</template>

<script setup>
import { ref, reactive, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'
import { exec } from 'kernelsu'
import * as KernelSU from '@/helpers/KernelSU'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsSwitch from '@/components/ui/SettingsSwitch.vue'
import SettingsNote from '@/components/ui/SettingsNote.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import ShieldIcon from '@/components/icons/Shield.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'

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

const { t, te } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const enabled = ref(false)
const ready = ref(false)
const readError = ref(false)
const saving = ref(false)
const items = ref(FALLBACK_ITEMS)
const matchedRules = ref([])
// Reading device_mitigation.json + the device props: 'loading' | 'ok' | 'error'.
const rulesState = ref('loading')
const device = reactive({ model: '', soc: '' })

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
    readError.value = true
  }
  ready.value = true

  try {
    const rules = JSON.parse(await KernelSU.readFile(RULES_FILE))
    const list = rules?.default?.items
    if (Array.isArray(list) && list.length) items.value = list.filter((i) => typeof i === 'string')

    const { stdout } = await exec(
      'tr -d "\\000" </proc/device-tree/model; echo; getprop ro.product.model; uname -r',
    )
    const [soc = '', model = '', uname = ''] = stdout.split('\n').map((l) => l.trim())
    device.soc = soc
    device.model = model
    matchedRules.value = Object.entries(rules?.device_rules || {})
      .filter(([, rule]) => ruleMatches(rule, { soc, model, uname }))
      .map(([id, rule]) => ({
        id,
        name: rule.name || id,
        description: rule.description || '',
        items: (rule.items || []).filter((i) => typeof i === 'string'),
      }))
    rulesState.value = 'ok'
  } catch (error) {
    console.error('Failed to read device mitigation rules:', error)
    rulesState.value = 'error'
  }
})

// Saved right away, like the other settings pages: closing the WebUI must not lose the change.
async function toggle(value) {
  if (saving.value) return
  saving.value = true
  enabled.value = value
  try {
    await fluxConfigStore.commit(() => fluxConfigStore.setDeviceMitigation(value))
    readError.value = false
    notify.success(
      t(value ? 'game_tweaks.saved_on' : 'game_tweaks.saved_off', {
        name: t('settings_page.device_mitigation.title'),
      }),
    )
  } catch (error) {
    console.error('Failed to set device mitigation:', error)
    notify.error(t('notify.save_failed'))
  } finally {
    enabled.value = fluxConfigStore.isDeviceMitigationEnabled
    saving.value = false
  }
}
</script>

<style scoped>
/* 3:2 on a phone, but never a full-width poster on tablets/desktop. */
.illustration {
  aspect-ratio: 3 / 2;
  max-height: 240px;
  width: 100%;
}

.item-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
</style>
