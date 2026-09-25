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
          <span class="hero-badge shape-sunny bg-secondary-container text-on-secondary-container">
            <GpuIcon :size="28" />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('gpu_governor.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-5">
          {{ $t('gpu_governor.brief') }}
        </p>

        <LoadingSpinner v-if="!probed" class="pt-6" :size="48" />

        <div v-else-if="!node" class="empty m3-card mb-5">
          <span
            class="empty-badge shape-clover4 bg-surface-container-highest text-on-surface-variant"
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

          <div class="space-y-3 mb-5">
            <GovernorPicker
              class="m3-enter"
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
              class="m3-enter"
              style="animation-delay: 60ms"
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

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('gpu_governor.apply_note') }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onActivated } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { exec } from 'kernelsu'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import GpuIcon from '@/components/icons/Gpu.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import BatterySaverIcon from '@/components/icons/BatterySaver.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import GovernorPicker from '@/components/ui/GovernorPicker.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

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
  }
  probe()
})
onActivated(() => probed.value && probe())

async function choose(profile, governor) {
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

  try {
    fluxConfigStore.setGpuGovernor(profile, governor)
    await fluxConfigStore.saveConfig()
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
