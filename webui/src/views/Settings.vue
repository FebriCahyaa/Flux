<template>
  <div class="page settings-page h-full flex flex-col overflow-hidden">
    <div class="sticky top-0 z-10 bg-background">
      <div class="max-w-3xl mx-auto px-5 pt-6 pb-3">
        <div class="flex justify-between items-center text-on-surface">
          <h1 class="m3-headline text-[32px]">{{ $t('settings_page.title') }}</h1>
        </div>
      </div>
    </div>

    <div class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll">
      <div class="max-w-3xl mx-auto p-5 py-1">
        <template v-for="section in sections" :key="section.key">
          <div class="px-4 py-2 mb-1">
            <h2 class="text-sm font-semibold text-primary">
              {{ $t(`settings_page.section.${section.key}`) }}
            </h2>
          </div>

          <div class="mb-4">
            <div v-for="item in section.items" :key="item.key" class="md3-list">
              <RippleComponent @click="item.run" class="md3-list-item" tabindex="0">
                <div class="flex items-center justify-between px-5 py-4">
                  <div class="flex items-center gap-4 min-w-0 flex-1">
                    <div class="entry-badge" :class="[item.shape, item.tone]">
                      <component :is="item.icon" :size="20" />
                    </div>
                    <div class="flex-1 min-w-0">
                      <h3 class="text-sm font-semibold text-on-surface">
                        {{ $t(`settings_page.${item.key}.title`) }}
                      </h3>
                      <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">
                        {{
                          item.subtitle
                            ? item.subtitle()
                            : $t(`settings_page.${item.key}.description`)
                        }}
                      </p>
                    </div>
                  </div>
                  <span
                    v-if="item.status && item.status()"
                    class="status ms-3"
                    :class="item.status().tone"
                    >{{ item.status().label }}</span
                  >
                  <div
                    v-else
                    class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3"
                  >
                    <ChevronRightIcon
                      class="text-on-surface-variant shrink-0 rtl:rotate-180"
                      :size="22"
                    />
                  </div>
                </div>
              </RippleComponent>
            </div>
          </div>
        </template>
      </div>
    </div>

    <!-- Export Modal -->
    <Modal
      :show="showExportModal"
      :title="$t('settings_page.save_log.title')"
      @close="closeExportModal"
      :closeOnOutsideClick="false"
    >
      <div class="px-4 pb-2">
        <div v-if="exportStatus === 'loading'" class="flex flex-col items-center gap-4 py-6">
          <LoadingSpinner :size="40" class="text-primary" />
          <p class="text-on-surface-variant text-sm">
            {{ $t('settings_page.save_log.exporting') }}
          </p>
        </div>

        <div v-else-if="exportStatus === 'success'" class="flex flex-col items-center gap-3 py-4">
          <CheckCircle :size="48" class="text-primary" />
          <p class="text-on-surface font-medium text-center">
            {{ $t('settings_page.save_log.success') }}
          </p>
          <p
            class="text-on-surface-variant text-xs break-all text-center bg-surface-container-low px-4 py-3 rounded-xl w-full font-mono"
          >
            {{ exportPath }}
          </p>
        </div>

        <div v-else-if="exportStatus === 'error'" class="flex flex-col items-center gap-3 py-4">
          <ErrorIcon :size="48" class="text-error" />
          <p class="text-on-surface font-medium text-center">
            {{ $t('settings_page.save_log.failure') }}
          </p>
          <p class="text-on-surface-variant text-sm text-center">{{ exportErrorMsg }}</p>
        </div>
      </div>

      <template #actions>
        <div v-if="exportStatus !== 'loading'" class="flex gap-2">
          <button
            @click="closeExportModal"
            class="px-4 py-2 text-sm font-semibold text-primary hover:bg-primary/10 rounded-full transition-colors"
          >
            {{ exportStatus === 'success' ? $t('common.ok') : $t('common.cancel') }}
          </button>
        </div>
      </template>
    </Modal>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useLanguageStore } from '@/stores/Language'
import { useFluxConfigStore } from '@/stores/FluxConfig'

import RippleComponent from '@/components/ui/Ripple.vue'
import ChevronRightIcon from '@/components/icons/ChevronRight.vue'
import LanguageIcon from '@/components/icons/Language.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import TextIcon from '@/components/icons/Text.vue'
import HomePlusIcon from '@/components/icons/HomePlus.vue'
import ContentSaveIcon from '@/components/icons/ContentSave.vue'
import ErrorIcon from '@/components/icons/Error.vue'
import Modal from '@/components/ui/Modal.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import CheckCircle from '@/components/icons/CheckCircle.vue'
import LeafIcon from '@/components/icons/Leaf.vue'
import PauseIcon from '@/components/icons/Pause.vue'
import GamesIcon from '@/components/icons/Games.vue'
import GpuIcon from '@/components/icons/Gpu.vue'
import ShieldIcon from '@/components/icons/Shield.vue'

import * as KernelSU from '@/helpers/KernelSU'
import { exec } from 'kernelsu'

const router = useRouter()
const { t } = useI18n()
const languageStore = useLanguageStore()

const showExportModal = ref(false)
const exportStatus = ref('idle')
const exportPath = ref('')
const exportErrorMsg = ref('')

const currentLanguage = computed(() => {
  if (languageStore.userPreference === null) {
    return t('language_selection.follow_system')
  } else {
    const langCode = languageStore.userPreference
    const langData = languageStore.availableLanguages[langCode]
    return langData?.name || langCode
  }
})

const fluxConfigStore = useFluxConfigStore()
onMounted(() => {
  if (!fluxConfigStore.isLoaded) fluxConfigStore.loadConfig().catch(() => {})
})

const go = (path) => () => router.push(`/settings/${path}`)
const tone = {
  primary: 'bg-primary-container text-on-primary-container',
  secondary: 'bg-secondary-container text-on-secondary-container',
  tertiary: 'bg-tertiary-container text-on-tertiary-container',
  error: 'bg-error-container text-on-error-container',
  neutral: 'bg-surface-container-highest text-on-surface',
}
// A small "On" pill on switches that change how every game runs.
const onPill = (value, danger = false) =>
  value
    ? {
        label: t('common.on'),
        tone: danger ? 'bg-error text-on-error' : 'bg-primary text-on-primary',
      }
    : null

// Each entry gets its own shape and colour so the list is easy to scan.
const sections = computed(() => [
  {
    key: 'preferences',
    items: [
      {
        key: 'lite_mode',
        icon: LeafIcon,
        shape: 'shape-flower',
        tone: tone.secondary,
        run: go('lite_mode'),
        status: () => onPill(fluxConfigStore.isLiteModeEnabled),
      },
      {
        key: 'game_tweaks',
        icon: GamesIcon,
        shape: 'shape-cookie12',
        tone: tone.tertiary,
        run: go('game_tweaks'),
      },
      {
        key: 'flux_boost',
        icon: BoltChargeIcon,
        shape: 'shape-burst',
        tone: tone.primary,
        run: go('flux_boost'),
      },
      {
        key: 'flux_sched',
        icon: TuneIcon,
        shape: 'shape-pentagon',
        tone: tone.secondary,
        run: go('flux_sched'),
      },
      {
        key: 'disable_tweaks',
        icon: PauseIcon,
        shape: 'shape-cookie6',
        tone: tone.error,
        run: go('disable_tweaks'),
        status: () => onPill(fluxConfigStore.isDisableTweaksEnabled, true),
      },
      {
        key: 'language',
        icon: LanguageIcon,
        shape: 'shape-circle',
        tone: tone.neutral,
        run: go('language'),
        subtitle: () => currentLanguage.value,
      },
    ],
  },
  {
    key: 'system',
    items: [
      {
        key: 'cpu_governor',
        icon: ChipsetIcon,
        shape: 'shape-cookie12',
        tone: tone.tertiary,
        run: go('cpu_governor'),
      },
      {
        key: 'gpu_governor',
        icon: GpuIcon,
        shape: 'shape-sunny',
        tone: tone.secondary,
        run: go('gpu_governor'),
      },
      {
        key: 'device_mitigation',
        icon: ShieldIcon,
        shape: 'shape-clover4',
        tone: tone.primary,
        run: go('device_mitigation'),
      },
      {
        key: 'log_level',
        icon: TextIcon,
        shape: 'shape-cookie4',
        tone: tone.neutral,
        run: go('log_level'),
      },
    ],
  },
  {
    key: 'others',
    items: [
      {
        key: 'save_log',
        icon: ContentSaveIcon,
        shape: 'shape-cookie9',
        tone: tone.primary,
        run: openExportModal,
      },
      {
        key: 'create_shortcut',
        icon: HomePlusIcon,
        shape: 'shape-flower',
        tone: tone.tertiary,
        run: () => KernelSU.createShortcut(),
      },
      {
        key: 'about',
        icon: InformationOutlineIcon,
        shape: 'shape-sunny',
        tone: tone.secondary,
        run: go('about'),
        subtitle: () => t('settings_page.about.entry'),
      },
    ],
  },
])

function openExportModal() {
  exportStatus.value = 'loading'
  exportPath.value = ''
  exportErrorMsg.value = ''
  showExportModal.value = true

  setTimeout(() => {
    exec(`/data/adb/modules/flux/system/bin/flux_utility save_logs`)
      .then(({ errno, stdout, stderr }) => {
        if (errno !== 0) {
          exportStatus.value = 'error'
          exportErrorMsg.value = stderr.trim()
        } else {
          exportStatus.value = 'success'
          exportPath.value = stdout.trim().split('\n').pop()
        }
      })
      .catch((err) => {
        console.error('Export error:', err)
        exportStatus.value = 'error'
        exportErrorMsg.value = err.message
      })
  }, 100)
}

const closeExportModal = () => {
  showExportModal.value = false
  setTimeout(() => {
    if (exportStatus.value !== 'loading') {
      exportStatus.value = 'idle'
      exportPath.value = ''
      exportErrorMsg.value = ''
    }
  }, 200)
}
</script>

<style scoped>
.entry-badge {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.status {
  flex-shrink: 0;
  font-size: 11px;
  font-weight: 700;
  padding: 3px 10px;
  border-radius: 999px;
}

.line-clamp-2 {
  display: -webkit-box;
  line-clamp: 2;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}
</style>
