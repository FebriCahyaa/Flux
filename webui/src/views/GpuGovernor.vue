<template>
  <SettingsDetailLayout
    :title="$t('gpu_governor.title')"
    :description="$t('gpu_governor.brief')"
    :icon="GpuIcon"
    shape="shape-sunny"
    tone="bg-secondary-container text-on-secondary-container"
    :read-error="readError"
  >
        <LoadingSpinner v-if="!probed" class="pt-6" :size="48" />

        <div v-else-if="!node" class="empty m3-card mb-5" role="status">
          <span
            class="empty-badge shape-clover4 bg-surface-container-highest text-on-surface-variant"
            aria-hidden="true"
          >
            <GpuIcon />
          </span>
          <div>
            <p class="text-sm font-semibold text-on-surface">
              {{ $t('gpu_governor.unsupported') }}
            </p>
            <p class="text-xs text-on-surface-variant mt-1">
              {{ $t('gpu_governor.unsupported_hint') }}
            </p>
          </div>
        </div>

        <template v-else>
          <div class="device m3-card mb-3">
            <div class="flex-1 min-w-0">
              <p class="text-[11px] font-semibold uppercase tracking-wider text-on-surface-variant">
                {{ $t('gpu_governor.running_now') }}
              </p>
              <p class="m3-headline text-xl text-on-surface mt-0.5 truncate">
                {{ current || '–' }}
              </p>
            </div>
            <div class="text-end min-w-0">
              <p class="text-[11px] font-semibold uppercase tracking-wider text-on-surface-variant">
                {{ $t('gpu_governor.kernel_default') }}
              </p>
              <p class="text-sm font-semibold text-on-surface mt-1 truncate">
                {{ kernelDefault || current || '–' }}
              </p>
            </div>
          </div>

          <!-- One picker per Flux profile (balanced / powersave), not the
               governor running now — that is shown in the card above. -->
          <div class="space-y-3 mb-5">
            <GovernorPicker
              :busy="saving"
              :title="$t('gpu_governor.balance_title')"
              :description="$t('gpu_governor.balance_description')"
              :icon="TuneIcon"
              shape="shape-cookie9"
              tone="bg-primary-container text-on-primary-container"
              :options="options"
              :model-value="gpuGovernor.balance"
              :risky-values="['performance']"
              @select="(g) => choose('balance', g)"
            />
            <GovernorPicker
              :busy="saving"
              :title="$t('gpu_governor.powersave_title')"
              :description="$t('gpu_governor.powersave_description')"
              :icon="BatterySaverIcon"
              shape="shape-flower"
              tone="bg-tertiary-container text-on-tertiary-container"
              :options="options"
              :model-value="gpuGovernor.powersave"
              :risky-values="['performance']"
              @select="(g) => choose('powersave', g)"
            />
          </div>
        </template>

        <SettingsNote>{{ $t('gpu_governor.apply_note') }}</SettingsNote>
  </SettingsDetailLayout>
</template>

<script setup>
import { ref, computed, onMounted, onActivated } from 'vue'
import { useI18n } from 'vue-i18n'
import { exec } from 'kernelsu'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import SettingsNote from '@/components/ui/SettingsNote.vue'
import GpuIcon from '@/components/icons/Gpu.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import GovernorPicker from '@/components/ui/GovernorPicker.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'

const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

const readError = ref(false)
const saving = ref(false)
const probed = ref(false)
const node = ref('')
const available = ref([])
const current = ref('')
const kernelDefault = ref('')

const gpuGovernor = computed(() => fluxConfigStore.gpuGovernor)
// '' = leave the kernel's own governor alone (the default)
const options = computed(() => [
  { value: '', label: t('gpu_governor.kernel_default') },
  ...available.value.map((g) => ({ value: g, label: g })),
])

// Same GPU devfreq nodes as gpu_governor_node() in flux_profiler. The kernel's
// governor is in the profiler's backup file once Flux has changed it.
const PROBE = `for d in /sys/class/kgsl/kgsl-3d0/devfreq /sys/class/devfreq/*gpu* /sys/class/devfreq/*mali* /sys/class/devfreq/*g3d*; do
  [ -f "$d/governor" ] || continue
  echo "$d"; cat "$d/available_governors"; cat "$d/governor"
  awk -v n="$d/governor" '$1 == "gpugov" && $2 == n { print $4; exit }' /dev/.flux_boost_orig 2>/dev/null
  break
done`

async function probe() {
  try {
    const { stdout } = await exec(PROBE)
    const [dir, govs, cur, orig] = (stdout || '').split('\n').map((s) => s.trim())
    node.value = dir || ''
    available.value = (govs || '').split(/\s+/).filter(Boolean)
    current.value = cur || ''
    kernelDefault.value = orig || ''
  } catch (error) {
    console.error('Failed to read GPU governors:', error)
  }
  probed.value = true
}

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
  } catch (error) {
    console.error('Failed to load config:', error)
    readError.value = true
  }
  probe()
})
onActivated(() => probed.value && probe())

async function choose(profile, governor) {
  if (saving.value) return
  if (governor === 'performance') {
    const ok = await notify.confirm({
      tone: 'danger',
      title: t('gpu_governor.confirm.performance_title'),
      message: t('gpu_governor.confirm.performance_message'),
      points: [t('gpu_governor.confirm.point_heat'), t('gpu_governor.confirm.point_battery')],
      confirmText: t('cpu_governor.modal.performance_warning_confirm'),
    })
    if (!ok) return
  } else if (governor === 'powersave' || governor === 'userspace') {
    const ok = await notify.confirm({
      tone: 'warning',
      title: t('gpu_governor.confirm.slow_title', { governor }),
      message: t('gpu_governor.confirm.slow_message'),
      confirmText: t('common.apply'),
    })
    if (!ok) return
  }

  saving.value = true
  try {
    await fluxConfigStore.commit(() => fluxConfigStore.setGpuGovernor(profile, governor))
    readError.value = false
    notify.success(
      t('gpu_governor.saved', {
        governor: governor || t('gpu_governor.kernel_default'),
        profile:
          profile === 'balance'
            ? t('gpu_governor.balance_title')
            : t('gpu_governor.powersave_title'),
      }),
    )
    setTimeout(probe, 700)
  } catch (error) {
    console.error('Failed to set GPU governor:', error)
    notify.error(t('notify.save_failed'))
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
.device {
  display: flex;
  gap: 16px;
  align-items: center;
  padding: 16px 20px;
  background: var(--color-surface-container-high);
}

.empty {
  display: flex;
  gap: 16px;
  align-items: center;
  padding: 20px;
}

.empty-badge {
  width: 52px;
  height: 52px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}
</style>
