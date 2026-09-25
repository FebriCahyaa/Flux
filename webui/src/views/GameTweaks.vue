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

        <p class="text-sm text-on-surface-variant leading-relaxed px-1 mb-6">
          {{ $t('game_tweaks.brief') }}
        </p>

        <div class="mb-6">
          <div
            v-for="(item, i) in items"
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
                  <span
                    v-if="item.key === 'touch_tweaks' && touchSupported === false"
                    class="tag bg-surface-container-highest text-on-surface-variant"
                    >{{ $t('game_tweaks.not_supported') }}</span
                  >
                  <span v-else-if="item.tag" class="tag" :class="item.tagTone">{{
                    $t(`game_tweaks.tags.${item.tag}`)
                  }}</span>
                </span>
                <span class="block text-xs text-on-surface-variant mt-1 leading-relaxed">{{
                  $t(`game_tweaks.${item.key}.description`)
                }}</span>
              </span>
              <ToggleSwitch
                :id="`tweak-${item.key}`"
                :model-value="values[item.key]"
                @update:modelValue="(v) => toggle(item, v)"
              />
            </div>
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
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { exec } from 'kernelsu'
import { useFluxConfigStore } from '@/stores/FluxConfig'
import { useNotifyStore } from '@/stores/Notify'

import ArrowLeftIcon from '@/components/icons/ArrowLeft.vue'
import GamesIcon from '@/components/icons/Games.vue'
import WifiIcon from '@/components/icons/Wifi.vue'
import TouchTapIcon from '@/components/icons/TouchTap.vue'
import SpeedIcon from '@/components/icons/Speed.vue'
import SparkleIcon from '@/components/icons/Sparkle.vue'
import InformationOutlineIcon from '@/components/icons/InformationOutline.vue'
import ToggleSwitch from '@/components/ui/ToggleSwitch.vue'

const router = useRouter()
const { t } = useI18n()
const fluxConfigStore = useFluxConfigStore()
const notify = useNotifyStore()

// Keys match FluxConfigStore::Preferences in fluxd. `confirmOn` asks before enabling.
const items = [
  {
    key: 'net_tweaks',
    icon: WifiIcon,
    shape: 'shape-cookie9',
    tone: 'bg-primary-container text-on-primary-container',
  },
  {
    key: 'touch_tweaks',
    icon: TouchTapIcon,
    shape: 'shape-flower',
    tone: 'bg-secondary-container text-on-secondary-container',
    tag: 'oplus',
    tagTone: 'bg-secondary-container text-on-secondary-container',
  },
  {
    key: 'game_refresh_rate',
    icon: SpeedIcon,
    shape: 'shape-sunny',
    tone: 'bg-tertiary-container text-on-tertiary-container',
    tag: 'battery',
    tagTone: 'bg-tertiary-container text-on-tertiary-container',
    confirmOn: true,
  },
  {
    key: 'drop_caches',
    icon: SparkleIcon,
    shape: 'shape-pentagon',
    tone: 'bg-surface-container-highest text-on-surface',
  },
]

const values = reactive({ ...fluxConfigStore.gameTweaks })
const touchSupported = ref(null)

onMounted(async () => {
  try {
    if (!fluxConfigStore.isLoaded) await fluxConfigStore.loadConfig()
    Object.assign(values, fluxConfigStore.gameTweaks)
  } catch (error) {
    console.error('Failed to load game tweaks:', error)
  }
  try {
    const { errno } = await exec('test -d /proc/touchpanel')
    touchSupported.value = errno === 0
  } catch {
    touchSupported.value = null
  }
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
  if (enabled && item.key === 'touch_tweaks' && touchSupported.value === false) {
    notify.warn(t('game_tweaks.touch_tweaks.unsupported_note'))
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

.tag {
  font-size: 10px;
  font-weight: 700;
  padding: 2px 8px;
  border-radius: 999px;
}
</style>
