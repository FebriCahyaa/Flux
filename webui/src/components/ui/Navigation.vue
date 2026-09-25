<template>
  <!--
    M3 Expressive navigation bar (phones) / rail (md+). The active indicator pill
    grows out of the icon on the default spatial spring; the icon bounces and the
    label gets heavier when selected.
  -->
  <nav ref="navEl"
    class="m3-nav footer fixed bottom-0 left-0 right-0 w-full flex items-end bg-surface-container z-50 md:left-0 md:top-0 md:bottom-0 md:w-24 md:h-full md:flex-col"
    :style="{
      paddingBottom: 'var(--window-inset-bottom, 0px)',
      paddingRight: 'var(--window-inset-right, 0px)',
      paddingLeft: 'var(--window-inset-left, 0px)'
    }">
    <div class="w-full h-20 flex items-center justify-center px-2 md:h-full md:flex-col md:justify-center md:gap-3 md:px-0">
      <router-link v-for="item in navItems" :key="item.name" :to="item.path"
        class="nav-item flex-1 max-w-40 flex flex-col items-center justify-center gap-1 no-underline select-none"
        :class="isActive(item) ? 'is-active text-on-surface' : 'text-on-surface-variant'"
        :aria-current="isActive(item) ? 'page' : undefined">
        <span class="indicator-wrap">
          <span class="indicator bg-secondary-container"></span>
          <span class="icon" :class="isActive(item) ? 'text-on-secondary-container' : ''">
            <component :is="item.icon" :active="isActive(item)" />
          </span>
        </span>
        <span class="label text-xs">{{ item.label }}</span>
      </router-link>
    </div>
  </nav>
</template>

<script setup>
import { computed, ref, onMounted, onBeforeUnmount } from 'vue'
import { useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'

import HomeIcon from '@/components/icons/Home.vue'
import GamesIcon from '@/components/icons/Games.vue'
import SettingsIcon from '@/components/icons/Settings.vue'
import MonitorIcon from '@/components/icons/Monitor.vue'

const { t } = useI18n()
const route = useRoute()

const navItems = computed(() => [
  {
    name: 'Home',
    path: '/',
    label: t('navigation.home'),
    icon: HomeIcon,
  },
  {
    name: 'Games',
    path: '/games',
    label: t('navigation.games'),
    icon: GamesIcon,
  },
  {
    name: 'Monitor',
    path: '/monitor',
    label: t('navigation.monitor'),
    icon: MonitorIcon,
  },
  {
    name: 'Settings',
    path: '/settings',
    label: t('navigation.settings'),
    icon: SettingsIcon,
  },
])

const isActive = (item) => {
  const currentPath = route.path
  if (item.path === '/') return currentPath === '/'
  return currentPath.startsWith(item.path)
}

const navEl = ref(null)
let ro = null

onMounted(() => {
  ro = new ResizeObserver(([entry]) => {
    const h = entry.borderBoxSize?.[0]?.blockSize ?? entry.target.offsetHeight
    document.documentElement.style.setProperty('--nav-height', `${h}px`)
  })
  ro.observe(navEl.value)
})

onBeforeUnmount(() => ro?.disconnect())
</script>

<style scoped>
.m3-nav {
  border-top-left-radius: 0;
}

.indicator-wrap {
  position: relative;
  width: 56px;
  height: 32px;
  display: grid;
  place-items: center;
}

.indicator {
  position: absolute;
  inset: 0;
  border-radius: 999px;
  transform: scaleX(0.3);
  opacity: 0;
  transition:
    transform var(--m3-spring-default-spatial-duration) var(--m3-spring-fast-spatial),
    opacity var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.is-active .indicator {
  transform: scaleX(1);
  opacity: 1;
}

.icon {
  position: relative;
  display: grid;
  place-items: center;
  transition: transform var(--m3-spring-fast-spatial-duration) var(--m3-spring-fast-spatial);
}

.is-active .icon {
  animation: nav-bounce var(--m3-spring-default-spatial-duration) var(--m3-spring-fast-spatial);
}

.nav-item:active .icon {
  transform: scale(0.88);
}

.label {
  font-weight: 500;
  transition:
    font-weight var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects),
    color var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
}

.is-active .label {
  font-weight: 750;
}

@keyframes nav-bounce {
  30% {
    transform: translateY(-3px) scale(1.08);
  }
}

@media (prefers-reduced-motion: reduce) {
  .indicator,
  .icon {
    transition: none;
    animation: none;
  }
}
</style>
