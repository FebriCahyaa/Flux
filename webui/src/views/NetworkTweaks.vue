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
          <span class="hero-badge shape-cookie9 bg-primary-container text-on-primary-container">
            <WifiIcon :size="28" />
          </span>
          <h1 class="m3-headline text-4xl text-on-surface">{{ $t('congestion_control.title') }}</h1>
        </div>

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-5">
          {{ $t('congestion_control.brief') }}
        </p>

        <LoadingSpinner v-if="!probed" class="pt-6" :size="48" />

        <div v-else-if="!options.length" class="empty m3-card mb-5">
          <span
            class="empty-badge shape-clover4 bg-surface-container-highest text-on-surface-variant"
          >
            <WifiIcon />
          </span>
          <div>
            <p class="text-sm font-semibold text-on-surface">
              {{ $t('congestion_control.unsupported') }}
            </p>
            <p class="text-xs text-on-surface-variant mt-1">
              {{ $t('congestion_control.unsupported_hint') }}
            </p>
          </div>
        </div>

        <template v-else>
          <div class="device m3-card mb-3">
            <div class="flex-1 min-w-0">
              <p class="text-[11px] font-semibold uppercase tracking-wider text-on-surface-variant">
                {{ $t('congestion_control.running_now') }}
              </p>
              <p class="m3-headline text-xl text-on-surface mt-0.5 truncate">
                {{ current || '–' }}
              </p>
            </div>
            <div class="text-end min-w-0">
              <p class="text-[11px] font-semibold uppercase tracking-wider text-on-surface-variant">
                {{ $t('congestion_control.recommended') }}
              </p>
              <p class="text-sm font-semibold text-on-surface mt-1 truncate">
                {{ recommended || '–' }}
              </p>
            </div>
          </div>

          <GovernorPicker
            class="m3-enter mb-5"
            :title="$t('congestion_control.picker_title')"
            :description="$t('congestion_control.picker_description', { algo: recommended || '–' })"
            :icon="WifiIcon"
            shape="shape-cookie9"
            tone="bg-primary-container text-on-primary-container"
            :options="options"
            :model-value="congestionControl"
            @select="choose"
          />
        </template>

        <div class="flex gap-3 px-1 mb-8">
          <InformationOutlineIcon class="text-on-surface-variant shrink-0" :size="20" />
          <p class="text-xs text-on-surface-variant leading-relaxed">
            {{ $t('congestion_control.apply_note') }}
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
import { useCapabilitiesStore } from '@/stores/Capabilities'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import WifiIcon from '@/components/icons/Wifi.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import GovernorPicker from '@/components/ui/GovernorPicker.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()
const capabilities = useCapabilitiesStore()

// Same priority order as flux_net()'s auto fallback in scripts/flux_profiler.sh.
const AUTO_PRIORITY = ['bbr3', 'bbr2', 'bbrplus', 'bbr', 'westwood', 'cubic']

const probed = ref(false)
const current = ref('')

const congestionControl = computed(() => fluxConfigStore.congestionControl)
const available = computed(() => (capabilities.caps?.net_cc || '').split(/\s+/).filter(Boolean))
const recommended = computed(() => AUTO_PRIORITY.find((a) => available.value.includes(a)) || available.value[0] || '')
// '' = auto (fluxd picks the best one this kernel offers)
const options = computed(() =>
  available.value.length
    ? [
        { value: '', label: t('congestion_control.auto') },
        ...available.value.map((a) => ({ value: a, label: a })),
      ]
    : [],
)

async function probe() {
  try {
    const { stdout } = await exec('cat /proc/sys/net/ipv4/tcp_congestion_control 2>/dev/null')
    current.value = (stdout || '').trim()
  } catch (error) {
    console.error('Failed to read current congestion control:', error)
  }
  probed.value = true
}

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
  } catch (error) {
    console.error('Failed to load config:', error)
  }
  await capabilities.load()
  probe()
})
onActivated(() => probed.value && probe())

async function choose(algo) {
  try {
    fluxConfigStore.setCongestionControl(algo)
    await fluxConfigStore.saveConfig()
    notify.success(
      t('congestion_control.saved', { algo: algo || t('congestion_control.auto') }),
    )
  } catch (error) {
    console.error('Failed to set congestion control:', error)
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
