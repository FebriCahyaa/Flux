<template>
  <div class="page games-page h-full flex flex-col overflow-hidden">
    <div class="max-w-3xl mx-auto h-full flex flex-col w-full">
      <!-- Header -->
      <div class="flex-none px-5 pt-6">
        <div class="flex justify-between items-end mb-4 text-on-surface">
          <h1 class="m3-headline text-[32px]">{{ $t('games_page.title') }}</h1>
          <span
            class="rounded-full bg-surface-container-high px-3 py-1 text-xs font-medium text-on-surface-variant"
          >
            {{ $t('games_page.count', myGames.length) }}
          </span>
        </div>

        <!-- Tabs: my games / all apps -->
        <div class="tabs mb-3" role="tablist">
          <button
            v-for="t in tabs"
            :key="t"
            role="tab"
            :aria-selected="tab === t"
            class="tab m3-press"
            :class="{ on: tab === t }"
            @click="tab = t"
          >
            {{ $t(`games_page.tabs.${t}`) }}
          </button>
        </div>

        <!-- Search -->
        <div class="bg-surface-container mb-3 px-4 py-3 rounded-full flex items-center gap-3">
          <SearchIcon class="text-on-surface-variant shrink-0" />
          <input
            v-model="gamesStore.searchQuery"
            type="text"
            :placeholder="$t('games_page.search_placeholder')"
            class="bg-transparent border-none outline-none text-on-surface placeholder-on-surface-variant w-full"
          />
          <button
            v-if="gamesStore.searchQuery"
            @click="gamesStore.searchQuery = ''"
            class="text-on-surface-variant hover:text-on-surface"
            :aria-label="$t('common.cancel')"
          >
            <CloseIcon class="w-5 h-5" />
          </button>
        </div>
      </div>

      <!-- List -->
      <div
        class="scrollbar-hidden pb-safe-nav flex-1 min-h-0 overflow-y-scroll px-4"
        ref="scrollContainer"
      >
        <LoadingSpinner class="pt-10" :size="56" v-if="gamesStore.isLoading" />

        <!-- My games -->
        <div v-else-if="tab === 'mine'" class="pb-4">
          <div v-if="!shownMine.length" class="empty m3-card">
            <span
              class="empty-badge shape-clover4 bg-secondary-container text-on-secondary-container"
              ><GamesIcon
            /></span>
            <div>
              <p class="text-sm font-semibold text-on-surface">
                {{
                  gamesStore.searchQuery
                    ? $t('games_page.no_apps_found')
                    : $t('games_page.empty_title')
                }}
              </p>
              <p v-if="!gamesStore.searchQuery" class="text-xs text-on-surface-variant mt-1">
                {{ $t('games_page.empty_hint') }}
              </p>
              <button v-if="!gamesStore.searchQuery" class="link" @click="tab = 'all'">
                {{ $t('games_page.tabs.all') }}
              </button>
            </div>
          </div>

          <div v-for="app in shownMine" :key="app.packageName" class="md3-list">
            <RippleComponent @click="openApp(app)" tabindex="0" class="md3-list-item">
              <div class="flex items-center gap-4 px-4 py-3.5">
                <img
                  :src="app.icon"
                  loading="lazy"
                  @error="iconError"
                  class="app-icon"
                  :alt="app.appName"
                />
                <div class="flex-1 min-w-0">
                  <h3 class="text-sm font-semibold text-on-surface truncate">
                    {{ app.appName || app.packageName }}
                  </h3>
                  <p class="text-xs text-on-surface-variant truncate mt-0.5">{{ subtitle(app) }}</p>
                  <div class="flex flex-wrap gap-1 mt-1.5">
                    <span
                      v-if="setting(app).lite_mode"
                      class="chip bg-tertiary-container text-on-tertiary-container"
                      >Lite</span
                    >
                    <span
                      v-if="setting(app).enable_dnd"
                      class="chip bg-secondary-container text-on-secondary-container"
                      >{{ $t('games_page.badges.dnd') }}</span
                    >
                    <span
                      v-if="stats(app)"
                      class="chip bg-surface-container-highest text-on-surface"
                    >
                      {{ formatDuration(stats(app).totalSeconds) }}
                    </span>
                  </div>
                </div>
                <div v-if="stats(app)?.fpsAvg" class="text-right shrink-0">
                  <p class="m3-headline text-xl text-on-surface tabular-nums">
                    {{ fmt(stats(app).fpsAvg) }}
                  </p>
                  <p class="text-[10px] text-on-surface-variant">{{ $t('sessions.avg_fps') }}</p>
                </div>
                <ChevronRightIcon
                  v-else
                  class="text-on-surface-variant shrink-0 rtl:rotate-180"
                  :size="22"
                />
              </div>
            </RippleComponent>
          </div>
        </div>

        <!-- All apps: add or remove with one tap -->
        <div v-else class="pb-4">
          <p class="text-xs text-on-surface-variant px-2 mb-3">{{ $t('games_page.all_hint') }}</p>
          <div v-if="!shownAll.length" class="text-center py-8 text-sm text-on-surface-variant">
            {{ $t('games_page.no_apps_found') }}
          </div>
          <div v-for="app in shownAll" :key="app.packageName" class="md3-list">
            <div class="md3-list-item flex items-center gap-4 px-4 py-3">
              <img
                :src="app.icon"
                loading="lazy"
                @error="iconError"
                class="app-icon sm"
                :alt="app.appName"
              />
              <button class="flex-1 min-w-0 text-start" @click="openApp(app)">
                <h3 class="text-sm font-semibold text-on-surface truncate">
                  {{ app.appName || app.packageName }}
                </h3>
                <p
                  v-if="app.appName && app.appName !== app.packageName"
                  class="text-xs text-on-surface-variant truncate"
                >
                  {{ app.packageName }}
                </p>
              </button>
              <button
                class="add-btn m3-press m3-press-morph"
                :class="
                  isMine(app)
                    ? 'bg-secondary-container text-on-secondary-container'
                    : 'bg-primary text-on-primary'
                "
                :disabled="busy[app.packageName]"
                @click="toggle(app)"
              >
                {{ isMine(app) ? $t('games_page.added') : $t('games_page.add') }}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onActivated, onDeactivated } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { useGamesStore } from '@/stores/Games'
import { useSessionsStore, formatDuration, fmt, relativeTime } from '@/stores/Sessions'

import LoadingSpinner from '@/components/ui/LoadingSpinner.vue'
import RippleComponent from '@/components/ui/Ripple.vue'
import SearchIcon from '@/components/icons/Search.vue'
import CloseIcon from '@/components/icons/Close.vue'
import ChevronRightIcon from '@/components/icons/ChevronRight.vue'
import GamesIcon from '@/components/icons/Games.vue'

const router = useRouter()
const { t, locale } = useI18n()
const gamesStore = useGamesStore()
const sessions = useSessionsStore()

const tabs = ['mine', 'all']
const tab = ref('mine')
const busy = reactive({})

const scrollContainer = ref(null)
let savedScrollTop = 0
onDeactivated(() => (savedScrollTop = scrollContainer.value?.scrollTop || 0))
onActivated(() => {
  if (savedScrollTop) scrollContainer.value?.scrollTo({ top: savedScrollTop })
  sessions.loadHistory()
})

onMounted(async () => {
  sessions.loadHistory()
  if (gamesStore.userApps.length === 0) {
    gamesStore.isLoading = true
    await gamesStore.initializeData()
  }
})

const isMine = (app) => app.packageName in gamesStore.gamelistConfig
const setting = (app) => gamesStore.gamelistConfig[app.packageName] || {}
const stats = (app) => sessions.statsFor(app.packageName)

// filteredApps is already searched and sorted (games first, then by name).
const myGames = computed(() => gamesStore.userApps.filter(isMine))
const shownMine = computed(() => {
  const list = gamesStore.filteredApps.filter(isMine)
  // Most recently played first; never-played games after them, by name.
  return [...list].sort((a, b) => (stats(b)?.lastStart ?? 0) - (stats(a)?.lastStart ?? 0))
})
// All apps: plain name order, so apps not yet added are not pushed below the games.
const shownAll = computed(() =>
  [...gamesStore.filteredApps].sort((a, b) =>
    (a.appName || a.packageName).localeCompare(b.appName || b.packageName),
  ),
)

function subtitle(app) {
  const s = stats(app)
  if (!s) return t('games_page.never_played')
  return t('games_page.last_played', { when: relativeTime(s.lastStart, locale.value) })
}

async function toggle(app) {
  busy[app.packageName] = true
  try {
    await gamesStore.toggleAppEnabled(app.packageName, !isMine(app))
  } catch (error) {
    console.error('Failed to update game list:', error)
  } finally {
    busy[app.packageName] = false
  }
}

const openApp = (app) => router.push(`/games/${app.packageName}`)
const iconError = (e) => (e.target.src = '/app_icon_fallback.avif')
</script>

<style scoped>
.tabs {
  display: flex;
  gap: 2px;
}

.tab {
  flex: 1;
  padding: 10px 12px;
  font-size: 14px;
  font-weight: 600;
  color: var(--color-on-surface-variant);
  background: var(--color-surface-container);
  border-radius: 8px;
  transition:
    border-radius var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial),
    background-color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.tab:first-child {
  border-radius: 20px 8px 8px 20px;
}

.tab:last-child {
  border-radius: 8px 20px 20px 8px;
}

.tab.on {
  background: var(--color-secondary-container);
  color: var(--color-on-secondary-container);
  border-radius: 999px;
}

.app-icon {
  width: 52px;
  height: 52px;
  flex-shrink: 0;
  border-radius: 16px;
  object-fit: cover;
}

.app-icon.sm {
  width: 44px;
  height: 44px;
  border-radius: 14px;
}

.chip {
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 999px;
}

.add-btn {
  flex-shrink: 0;
  min-width: 84px;
  padding: 8px 14px;
  border-radius: 999px;
  font-size: 13px;
  font-weight: 650;
}

.empty {
  display: flex;
  gap: 16px;
  align-items: center;
  padding: 20px;
  margin-bottom: 8px;
}

.empty-badge {
  width: 52px;
  height: 52px;
  display: grid;
  place-items: center;
  flex-shrink: 0;
}

.link {
  margin-top: 8px;
  font-size: 13px;
  font-weight: 650;
  color: var(--color-primary);
}
</style>
