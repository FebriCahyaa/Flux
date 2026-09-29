<template>
  <section class="game-runtime" aria-labelledby="gr-title">
    <h2 id="gr-title" class="text-sm font-semibold text-primary px-4 pt-5 pb-2">
      {{ $t('game_runtime.title') }}
    </h2>

    <!-- Performance -->
    <div class="md3-list" v-for="f in perfFields" :key="f.key">
      <div class="md3-list-item flex items-center gap-4 px-5 py-3.5 cursor-default">
        <label :for="`gr-${f.key}`" class="flex-1 min-w-0 text-sm font-semibold text-on-surface">
          {{ $t(`game_runtime.${f.key}`) }}
          <span class="block text-xs font-normal text-on-surface-variant">{{
            $t(`game_runtime.${f.key}_hint`)
          }}</span>
        </label>
        <select
          :id="`gr-${f.key}`"
          class="gr-select"
          :value="perf[f.key] ?? ''"
          :disabled="busy"
          :aria-busy="busy || undefined"
          @change="(e) => set('performance', f.key, e.target.value)"
        >
          <option value="">{{ $t('game_runtime.inherit') }}</option>
          <option v-for="o in f.options" :key="o" :value="o">
            {{ $t(`game_runtime.opt.${o}`) }}
          </option>
        </select>
      </div>
    </div>

    <SettingsSwitch
      :model-value="!!perf.launch_boost"
      :title="$t('game_runtime.launch_boost')"
      :description="$t('game_runtime.launch_boost_hint')"
      :disabled="busy"
      variant="row"
      @update:model-value="(v) => set('performance', 'launch_boost', v)"
    />

    <!-- Compatibility -->
    <h2 class="text-sm font-semibold text-primary px-4 pt-5 pb-2">
      {{ $t('game_runtime.compat_title') }}
    </h2>
    <div class="md3-list">
      <div class="md3-list-item flex items-center gap-4 px-5 py-3.5 cursor-default">
        <label for="gr-mode" class="flex-1 min-w-0 text-sm font-semibold text-on-surface">
          {{ $t('game_runtime.mode') }}
          <span class="block text-xs font-normal text-on-surface-variant">{{
            $t('game_runtime.mode_hint')
          }}</span>
        </label>
        <select
          id="gr-mode"
          class="gr-select"
          :value="compat.mode ?? 'auto'"
          :disabled="busy"
          @change="(e) => onMode(e.target.value)"
        >
          <option v-for="m in modes" :key="m" :value="m">{{ $t(`game_runtime.mode_${m}`) }}</option>
        </select>
      </div>
    </div>

    <template v-if="showIdentities">
      <div class="md3-list" v-for="layer in ['device', 'cpu', 'gpu']" :key="layer">
        <div class="md3-list-item flex items-center gap-4 px-5 py-3.5 cursor-default">
          <label
            :for="`gr-id-${layer}`"
            class="flex-1 min-w-0 text-sm font-semibold text-on-surface"
          >
            {{ $t(`game_runtime.identity_${layer}`) }}
          </label>
          <select
            :id="`gr-id-${layer}`"
            class="gr-select"
            :value="compat[`${layer}_profile`] ?? ''"
            :disabled="busy"
            @change="(e) => set('compatibility', `${layer}_profile`, e.target.value)"
          >
            <option value="">{{ $t(`game_runtime.real_${layer}`) }}</option>
            <option v-for="n in store.identityNames(layer)" :key="n" :value="n">{{ n }}</option>
          </select>
        </div>
      </div>
    </template>

    <!-- Backend -->
    <SettingsSwitch
      :model-value="store.zygiskOptIn"
      :title="$t('game_runtime.zygisk_title')"
      :description="$t('game_runtime.zygisk_hint')"
      :disabled="busy"
      variant="row"
      @update:model-value="setZygisk"
    />
    <p class="text-xs text-on-surface-variant px-5 pt-2" role="note">
      {{ $t('game_runtime.risk_note') }}
    </p>

    <!-- Diagnostics -->
    <h2 class="text-sm font-semibold text-primary px-4 pt-5 pb-2">
      {{ $t('game_runtime.diag_title') }}
    </h2>
    <div class="m3-card p-5 mb-8" :aria-busy="store.analysisStatus === 'loading' || undefined">
      <button
        type="button"
        class="m3-press pill bg-secondary-container text-on-secondary-container"
        :disabled="store.analysisStatus === 'loading'"
        @click="store.analyze(pkg, compat.mode)"
      >
        {{
          store.analysisStatus === 'loading'
            ? $t('game_runtime.analyzing')
            : $t('game_runtime.analyze')
        }}
      </button>

      <p v-if="store.analysisStatus === 'error'" class="text-sm text-error mt-3" role="alert">
        {{ $t('game_runtime.analysis_error') }}
      </p>

      <div v-if="res" class="mt-4 space-y-3 text-sm">
        <p class="text-on-surface-variant">
          {{ res.known_game ? $t('game_runtime.known_game') : $t('game_runtime.unknown_game') }}
          · {{ $t('game_runtime.confidence') }}: {{ $t(`game_runtime.conf_${res.confidence}`) }}
        </p>
        <p v-if="res.recommendation === 'create_profile'" class="text-on-surface">
          {{ $t('game_runtime.recommend_create') }}
        </p>

        <!-- Unlocked / Capable / Sustained are three separate answers -->
        <dl class="grid grid-cols-3 gap-2 text-center">
          <div
            v-for="k in ['unlocked', 'capable', 'sustained']"
            :key="k"
            class="tile bg-surface-container"
          >
            <dt class="text-xs text-on-surface-variant">{{ $t(`game_runtime.${k}`) }}</dt>
            <dd class="font-semibold">{{ $t(`game_runtime.tri_${res[k]}`) }}</dd>
          </div>
        </dl>

        <ul class="space-y-1">
          <li v-for="l in res.layers" :key="l.layer" class="flex justify-between gap-3">
            <span>{{ $t(`game_runtime.layer_${l.layer}`) }}</span>
            <span class="text-right">
              <span class="font-semibold">{{ $t(`game_runtime.state_${l.state}`) }}</span>
              <span class="block text-xs text-on-surface-variant break-words">{{ l.reason }}</span>
            </span>
          </li>
        </ul>

        <ul v-if="res.blockers.length" class="text-xs text-error space-y-1">
          <li v-for="b in res.blockers" :key="b.kind + b.reason">
            {{ $t(`game_runtime.gate_${b.kind}`) }}: {{ b.reason }}
          </li>
        </ul>

        <div class="text-xs text-on-surface-variant">
          <p>
            {{ $t('game_runtime.real_hardware') }}: {{ real.soc || '–' }} ·
            {{ real.gpu.trim() || '–' }} · Vulkan {{ $t(`game_runtime.tri_${real.vulkan}`) }} ·
            {{ real.peak_refresh_hz || '–' }} Hz
          </p>
          <p>
            {{ $t('game_runtime.backend') }}: {{ $t('game_runtime.backend_native') }} · Zygisk
            {{ $t(`game_runtime.backend_${store.analysis.backends.zygisk}`) }}
          </p>
        </div>
      </div>
    </div>
  </section>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useGameRuntimeStore } from '@/stores/GameRuntime'
import { useNotifyStore } from '@/stores/Notify'
import SettingsSwitch from '@/components/ui/SettingsSwitch.vue'

const props = defineProps({ pkg: { type: String, required: true } })
const { t } = useI18n()
const store = useGameRuntimeStore()
const notify = useNotifyStore()
const busy = ref(false)

const modes = ['real', 'auto', 'compatibility', 'advanced', 'custom']
const perfFields = [
  { key: 'profile', options: ['performance', 'balance', 'powersave'] },
  { key: 'memory', options: ['default', 'balanced', 'gaming', 'gaming_plus'] },
  {
    key: 'touch',
    options: ['default', 'balanced', 'responsive', 'responsive_plus', 'competitive'],
  },
  { key: 'storage', options: ['default', 'balanced', 'gaming'] },
  { key: 'refresh', options: ['real', 'adaptive', 'hz60', 'hz90', 'hz120'] },
]

const perf = computed(() => store.profileFor(props.pkg).performance)
const compat = computed(() => store.profileFor(props.pkg).compatibility)
const res = computed(() => store.analysis?.resolution)
const real = computed(() => store.analysis?.real_hardware)
// Identity pickers only make sense when the user chooses layers themselves.
const showIdentities = computed(() =>
  ['compatibility', 'advanced', 'custom'].includes(compat.value.mode),
)

onMounted(() => {
  if (!store.loaded) store.load()
})

async function set(section, key, value) {
  if (busy.value) return
  busy.value = true
  try {
    await store.setField(props.pkg, section, key, value)
  } catch (e) {
    console.error(`Failed to save ${key}:`, e)
    notify.error(t('notify.save_failed'))
  } finally {
    busy.value = false
  }
}

const onMode = (m) => set('compatibility', 'mode', m)

async function setZygisk(enabled) {
  busy.value = true
  try {
    await store.setZygiskOptIn(enabled)
  } catch (e) {
    console.error('Failed to change the Zygisk opt-in:', e)
    notify.error(t('notify.save_failed'))
  } finally {
    busy.value = false
  }
}
</script>

<style scoped>
.gr-select {
  min-height: 40px;
  max-width: 48%;
  padding: 0 12px;
  border-radius: 12px;
  color: var(--color-on-surface);
  background: var(--color-surface-container-highest);
  border: 1px solid var(--color-outline-variant);
  font-size: 14px;
}
.gr-select:focus-visible,
.pill:focus-visible {
  outline: 2px solid var(--color-primary);
  outline-offset: 2px;
}
.gr-select:disabled,
.pill:disabled {
  opacity: 0.5;
}
.pill {
  display: inline-flex;
  padding: 10px 18px;
  border-radius: 999px;
  font-size: 14px;
  font-weight: 650;
}
.tile {
  border-radius: 16px;
  padding: 10px;
}
</style>
