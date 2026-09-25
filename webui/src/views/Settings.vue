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

        <!-- Preferences -->
        <div class="px-4 py-2 mb-1">
          <h2 class="text-sm font-semibold text-primary">
            {{ $t('settings_page.section.preferences') }}
          </h2>
        </div>

        <div class="space-y-1.5 mb-4">
          <div class="md3-list">
            <RippleComponent @click="openLiteModeView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <FeatherIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.lite_mode.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.lite_mode.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>

          <div class="md3-list">
            <RippleComponent @click="openDisableTweaksView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <TuneIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.disable_tweaks.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.disable_tweaks.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>

          <div class="md3-list">
            <RippleComponent @click="openFluxSchedView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <ChipsetIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.flux_sched.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.flux_sched.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>

          <div class="md3-list">
            <RippleComponent @click="openFluxBoostView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <BoltChargeIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.flux_boost.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.flux_boost.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>

          <div class="md3-list">
            <RippleComponent @click="openLanguageView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <LanguageIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.language.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1">{{ currentLanguage }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>
        </div>

        <!-- System -->
        <div class="px-4 py-2 mb-1">
          <h2 class="text-sm font-semibold text-primary">
            {{ $t('settings_page.section.system') }}
          </h2>
        </div>

        <div class="space-y-1.5 mb-4">
          <div class="md3-list">
            <RippleComponent @click="openDeviceMitigationView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <BugIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.device_mitigation.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.device_mitigation.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>

          <div class="md3-list">
            <RippleComponent @click="openCpuGovernorView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <ChipsetIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.cpu_governor.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.cpu_governor.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>

          <div class="md3-list">
            <RippleComponent @click="openLogLvlView" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <TextIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.log_level.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.log_level.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>
        </div>

        <!-- Others -->
        <div class="px-4 py-2 mb-1">
          <h2 class="text-sm font-semibold text-primary">
            {{ $t('settings_page.section.others') }}
          </h2>
        </div>

        <div class="space-y-1.5 mb-4">
          <div class="md3-list">
            <RippleComponent @click="openExportModal" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <ContentSaveIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.save_log.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.save_log.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>

          <div class="md3-list">
            <RippleComponent @click="createShortcut" class="md3-list-item" tabindex="0">
              <div class="flex items-center justify-between px-5 py-4">
                <div class="flex items-center gap-4 min-w-0 flex-1">
                  <div class="w-10 h-10 shape-cookie9 bg-primary-container flex items-center justify-center shrink-0">
                    <HomePlusIcon class="w-5 h-5 text-on-primary-container" />
                  </div>
                  <div class="flex-1 min-w-0">
                    <h3 class="text-sm font-semibold text-on-surface">{{ $t('settings_page.create_shortcut.title') }}</h3>
                    <p class="text-xs text-on-surface-variant mt-1 line-clamp-2">{{ $t('settings_page.create_shortcut.description') }}</p>
                  </div>
                </div>
                <div class="w-7 h-7 rounded-full bg-surface-dim flex items-center justify-center shrink-0 ms-3">
                  <ChevronRightIcon class="text-on-surface-variant shrink-0 rtl:rotate-180" :size="22" />
                </div>
              </div>
            </RippleComponent>
          </div>
        </div>

        <!-- ── About Developer ─────────────────────────────────────────────── -->
        <div class="px-4 py-2 mb-1">
          <h2 class="text-sm font-semibold text-primary">
            {{ $t('settings_page.section.about') }}
          </h2>
        </div>

        <!-- Developer card (M3 Expressive) -->
        <section class="about-card m3-enter bg-surface-container-high rounded-[32px] p-5 mb-3 relative overflow-hidden">
          <span class="about-deco shape-flower bg-primary" aria-hidden="true"></span>

          <div class="relative flex items-center gap-4">
            <div class="about-avatar shape-cookie12 bg-primary-container text-on-primary-container grid place-items-center shrink-0">
              <CodeIcon :size="30" />
            </div>
            <div class="min-w-0">
              <h3 class="m3-headline text-2xl text-on-surface">FebriCahyaa</h3>
              <p class="text-xs text-on-surface-variant mt-1">{{ $t('settings_page.about.role') }}</p>
              <span class="mt-2 inline-flex items-center gap-1.5 rounded-full bg-primary text-on-primary px-2.5 py-1 text-xs font-semibold">
                <span class="about-dot"></span>{{ $t('settings_page.about.status') }}
              </span>
            </div>
          </div>

          <!-- Facts: segmented tonal tiles -->
          <div class="about-facts relative grid grid-cols-2 gap-0.5 mt-5">
            <div class="fact bg-secondary-container text-on-secondary-container">
              <p class="text-xs opacity-80">{{ $t('settings_page.about.module_name') }}</p>
              <p class="text-sm font-bold mt-1">Flux Tweaks</p>
            </div>
            <div class="fact bg-tertiary-container text-on-tertiary-container">
              <p class="text-xs opacity-80">{{ $t('settings_page.about.platform') }}</p>
              <p class="text-sm font-bold mt-1">Magisk · KSU · APatch</p>
            </div>
            <div class="fact bg-surface-container-highest text-on-surface">
              <p class="text-xs text-on-surface-variant">{{ $t('settings_page.about.target') }}</p>
              <p class="text-sm font-bold mt-1">GKI &amp; Non-GKI</p>
            </div>
            <div class="fact bg-primary-container text-on-primary-container">
              <p class="text-xs opacity-80">{{ $t('settings_page.about.license') }}</p>
              <p class="text-sm font-bold mt-1">Apache 2.0</p>
            </div>
          </div>

          <p class="relative text-sm text-on-surface-variant leading-relaxed mt-4 px-1">
            {{ $t('settings_page.about.description') }}
          </p>

          <!-- Button group: tonal + filled, pills that morph on press -->
          <div class="relative flex gap-2 mt-5">
            <RippleComponent
              @click="openGithub"
              class="m3-press m3-press-morph flex-1 flex items-center justify-center gap-2 rounded-full bg-secondary-container text-on-secondary-container py-3.5 cursor-pointer"
              tabindex="0"
            >
              <GithubIcon :size="18" />
              <span class="text-sm font-semibold">GitHub</span>
            </RippleComponent>
            <RippleComponent
              @click="openTelegram"
              class="m3-press m3-press-morph flex-1 flex items-center justify-center gap-2 rounded-full bg-primary text-on-primary py-3.5 cursor-pointer"
              tabindex="0"
            >
              <TelegramIcon :size="18" />
              <span class="text-sm font-semibold">Telegram</span>
            </RippleComponent>
          </div>
        </section>

        <!-- Module tagline -->
        <div class="text-center py-4 mb-2">
          <p class="m3-headline text-lg text-primary">"{{ $t('settings_page.about.tagline') }}"</p>
          <p class="text-xs text-on-surface-variant opacity-60 mt-1">Flux Tweaks · FebriCahyaa</p>
        </div>

      </div>
    </div>

    <!-- Export Modal -->
    <Modal :show="showExportModal" :title="$t('settings_page.save_log.title')" @close="closeExportModal"
      :closeOnOutsideClick="false">
      <div class="px-4 pb-2">
        <div v-if="exportStatus === 'loading'" class="flex flex-col items-center gap-4 py-6">
          <LoadingSpinner :size="40" class="text-primary" />
          <p class="text-on-surface-variant text-sm">{{ $t('settings_page.save_log.exporting') }}</p>
        </div>

        <div v-else-if="exportStatus === 'success'" class="flex flex-col items-center gap-3 py-4">
          <CheckCircle :size="48" class="text-primary" />
          <p class="text-on-surface font-medium text-center">{{ $t('settings_page.save_log.success') }}</p>
          <p class="text-on-surface-variant text-xs break-all text-center bg-surface-container-low px-4 py-3 rounded-xl w-full font-mono">
            {{ exportPath }}
          </p>
        </div>

        <div v-else-if="exportStatus === 'error'" class="flex flex-col items-center gap-3 py-4">
          <ErrorIcon :size="48" class="text-error" />
          <p class="text-on-surface font-medium text-center">{{ $t('settings_page.save_log.failure') }}</p>
          <p class="text-on-surface-variant text-sm text-center">{{ exportErrorMsg }}</p>
        </div>
      </div>

      <template #actions>
        <div v-if="exportStatus !== 'loading'" class="flex gap-2">
          <button @click="closeExportModal"
            class="px-4 py-2 text-sm font-semibold text-primary hover:bg-primary/10 rounded-full transition-colors">
            {{ exportStatus === 'success' ? $t('common.ok') : $t('common.cancel') }}
          </button>
        </div>
      </template>
    </Modal>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useLanguageStore } from '@/stores/Language'

import RippleComponent from '@/components/ui/Ripple.vue'
import ChevronRightIcon from '@/components/icons/ChevronRight.vue'
import LanguageIcon from '@/components/icons/Language.vue'
import FeatherIcon from '@/components/icons/Feather.vue'
import TuneIcon from '@/components/icons/Tune.vue'
import ChipsetIcon from '@/components/icons/Chipset.vue'
import BoltChargeIcon from '@/components/icons/BoltCharge.vue'
import BugIcon from '@/components/icons/Bug.vue'
import TextIcon from '@/components/icons/Text.vue'
import HomePlusIcon from '@/components/icons/HomePlus.vue'
import ContentSaveIcon from '@/components/icons/ContentSave.vue'
import ErrorIcon from '@/components/icons/Error.vue'
import Modal from '@/components/ui/Modal.vue'
import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import CheckCircle from '@/components/icons/CheckCircle.vue'
import CodeIcon from '@/components/icons/Code.vue'
import GithubIcon from '@/components/icons/Github.vue'
import TelegramIcon from '@/components/icons/Telegram.vue'

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

const openLiteModeView = () => router.push('/settings/lite_mode')
const openLanguageView = () => router.push('/settings/language')
const openDeviceMitigationView = () => router.push('/settings/device_mitigation')
const openCpuGovernorView = () => router.push('/settings/cpu_governor')
const openLogLvlView = () => router.push('/settings/log_level')
const openDisableTweaksView = () => router.push('/settings/disable_tweaks')
const openFluxSchedView = () => router.push('/settings/flux_sched')
const openFluxBoostView = () => router.push('/settings/flux_boost')
const createShortcut = () => KernelSU.createShortcut()

const openGithub = () => KernelSU.openWebsite('https://github.com/FebriCahyaa/Flux')
const openTelegram = () => KernelSU.openWebsite('https://t.me/c/3901105851/3')

const openExportModal = () => {
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
          exportPath.value = stdout.trim()
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
.line-clamp-2 {
  display: -webkit-box;
  line-clamp: 2;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.about-deco {
  position: absolute;
  width: 180px;
  height: 180px;
  right: -60px;
  top: -70px;
  opacity: 0.1;
  animation: about-spin 40s linear infinite;
}

.about-avatar {
  width: 72px;
  height: 72px;
  transition: clip-path var(--m3-spring-slow-spatial-duration) var(--m3-spring-default-spatial);
}

.about-card:active .about-avatar {
  clip-path: var(--m3-shape-burst);
}

.about-dot {
  width: 6px;
  height: 6px;
  border-radius: 999px;
  background: currentColor;
  animation: about-pulse 1.6s ease-in-out infinite;
}

/* Segmented 2x2 group: large outer corners, small inner ones */
.about-facts .fact {
  padding: 14px 16px;
  border-radius: 6px;
}

.about-facts .fact:nth-child(1) {
  border-top-left-radius: 24px;
}

.about-facts .fact:nth-child(2) {
  border-top-right-radius: 24px;
}

.about-facts .fact:nth-child(3) {
  border-bottom-left-radius: 24px;
}

.about-facts .fact:nth-child(4) {
  border-bottom-right-radius: 24px;
}

@keyframes about-spin {
  to {
    transform: rotate(1turn);
  }
}

@keyframes about-pulse {
  50% {
    opacity: 0.35;
  }
}

@media (prefers-reduced-motion: reduce) {
  .about-deco,
  .about-dot {
    animation: none;
  }
}
</style>
