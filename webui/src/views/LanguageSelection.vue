<template>
  <SettingsDetailLayout :title="$t('language_selection.title')">
    <!-- Native radio inputs (RadioButton) inside a labelled group. The whole
         row is tappable; selectLanguage ignores the repeat call a tap on the
         label also produces. -->
    <div
      role="radiogroup"
      :aria-label="$t('language_selection.title')"
      :aria-busy="switching || undefined"
      class="mb-8"
    >
      <div class="md3-list">
        <div
          class="md3-list-item lang-row px-5 py-3.5 cursor-pointer"
          @click="selectLanguage('system')"
        >
          <RadioButton
            :model-value="selectedLanguage"
            value="system"
            :name="radioGroupName"
            :label="$t('language_selection.follow_system')"
            :disabled="switching"
            @update:model-value="selectLanguage"
          />
        </div>
      </div>

      <div v-for="language in filteredAndSortedLanguages" :key="language.code" class="md3-list">
        <div
          class="md3-list-item lang-row px-5 py-3.5 cursor-pointer"
          :lang="language.code.replace('_', '-')"
          @click="selectLanguage(language.code)"
        >
          <RadioButton
            :model-value="selectedLanguage"
            :value="language.code"
            :name="radioGroupName"
            :label="language.name"
            :disabled="switching"
            @update:model-value="selectLanguage"
          />
        </div>
      </div>
    </div>
  </SettingsDetailLayout>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { useLanguageStore } from '@/stores/Language'
import { useNotifyStore } from '@/stores/Notify'
import { detectBrowserLocale, checkLanguageFile } from '@/helpers/Locales'

import SettingsDetailLayout from '@/components/ui/SettingsDetailLayout.vue'
import RadioButton from '@/components/ui/RadioButton.vue'

const { t } = useI18n()
const languageStore = useLanguageStore()
const notify = useNotifyStore()

const selectedLanguage = ref('')
const switching = ref(false)
const radioGroupName = 'language-selection-group'

const languagesWithMissingFiles = ref([])

const filteredAndSortedLanguages = computed(() => {
  const langArray = languageStore.getAvailableLanguages()

  const filtered = langArray.filter((lang) => !languagesWithMissingFiles.value.includes(lang.code))

  const english = filtered.find((lang) => lang.code === 'en')
  const others = filtered
    .filter((lang) => lang.code !== 'en')
    .sort((a, b) => a.code.localeCompare(b.code))

  return english ? [english, ...others] : others
})

onMounted(async () => {
  const allLanguages = languageStore.getAvailableLanguages()

  const checkPromises = allLanguages.map(async (lang) => {
    const hasFile = await checkLanguageFile(lang.code)
    if (!hasFile) {
      languagesWithMissingFiles.value.push(lang.code)
    }
    return { code: lang.code, hasFile }
  })

  await Promise.all(checkPromises)

  if (languageStore.userPreference === null) {
    selectedLanguage.value = 'system'
  } else {
    if (languagesWithMissingFiles.value.includes(languageStore.userPreference)) {
      selectedLanguage.value = 'system'
      await languageStore.setLanguage(detectBrowserLocale(), false)
    } else {
      selectedLanguage.value = languageStore.userPreference
    }
  }
})

watch(
  () => languageStore.userPreference,
  (newPreference) => {
    if (newPreference === null) {
      selectedLanguage.value = 'system'
    } else if (!languagesWithMissingFiles.value.includes(newPreference)) {
      selectedLanguage.value = newPreference
    } else {
      selectedLanguage.value = 'system'
    }
  },
  { immediate: true },
)

// A tap on a row's label reaches here up to three times (label click, the
// radio's change, and the synthetic input click bubbling to the row); the
// guards keep it to one locale load.
async function selectLanguage(languageCode) {
  if (switching.value || languageCode === selectedLanguage.value) return
  if (languageCode !== 'system' && languagesWithMissingFiles.value.includes(languageCode)) {
    console.warn(`Cannot set language ${languageCode}: translation file missing`)
    return
  }

  const previous = selectedLanguage.value
  selectedLanguage.value = languageCode
  switching.value = true
  try {
    const success =
      languageCode === 'system'
        ? await languageStore.setLanguage(detectBrowserLocale(), false)
        : await languageStore.setLanguage(languageCode, true)
    if (!success) {
      selectedLanguage.value = previous
      notify.error(t('language_selection.load_failed'))
    }
  } finally {
    switching.value = false
  }
}
</script>

<style scoped>
/* RadioButton is inline-flex; let it take the row so the label text wraps
   instead of overflowing on narrow screens. */
.lang-row > div {
  display: flex;
  width: 100%;
}
</style>
