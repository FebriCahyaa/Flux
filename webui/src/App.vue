<template>
  <div id="app" class="copy-protected min-h-screen flex flex-col bg-background text-on-background overflow-hidden">
    <main class="main-content flex-1 md:ml-20 overflow-hidden relative">
      <router-view v-slot="{ Component, route }">
        <transition :name="transitionName" @after-enter="onAfterEnter">
          <keep-alive>
            <component :is="Component" :key="route.path" ref="pageComponent" />
          </keep-alive>
        </transition>
      </router-view>
    </main>
    <Navigation />
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import Navigation from '@/components/ui/Navigation.vue'

const route = useRoute()
const transitionName = ref('')
const pageComponent = ref(null)

// Define top-level routes that should NOT animate between each other
const topLevelRoutes = ['/', '/games', '/monitor', '/settings']

// Triggered when the enter transition finishes
const onAfterEnter = () => {
  if (pageComponent.value && typeof pageComponent.value.onPageReady === 'function') {
    pageComponent.value.onPageReady()
  }
}

watch(
  () => route.path,
  (to, from) => {
    // Between top-level destinations: M3 fade-through
    if (topLevelRoutes.includes(to) && topLevelRoutes.includes(from)) {
      transitionName.value = 'fade-through'
      return
    }

    // If the 'to' path contains the 'from' path, we are going deeper (Opening Child)
    // Example: /settings -> /settings/lite_mode
    const isOpeningChild = to.startsWith(from === '/' ? '' : from) && to.length > from.length

    // If the 'from' path contains the 'to' path, we are going back (Closing Child)
    // Example: /settings/lite_mode -> /settings
    const isClosingChild = from.startsWith(to === '/' ? '' : to) && from.length > to.length

    if (isOpeningChild) {
      transitionName.value = 'page-open'
    } else if (isClosingChild) {
      transitionName.value = 'page-close'
    } else {
      transitionName.value = ''
    }
  },
)
</script>

<style>
.page-open-enter-active,
.page-open-leave-active,
.page-close-enter-active,
.page-close-leave-active {
  /* Spatial spring for movement, effects spring for fades (M3 Expressive motion) */
  transition:
    transform var(--m3-spring-default-spatial-duration) var(--m3-spring-default-spatial),
    opacity var(--m3-spring-default-effects-duration) var(--m3-spring-default-effects);
  position: absolute;
  width: 100%;
  top: var(--window-inset-top, 0px);
  bottom: 0;
  left: 0;
  will-change: transform, opacity;
  background-color: var(--color-background);
}

.page-open-enter-active {
  z-index: 2;
}

.page-open-leave-active {
  z-index: 1;
}

.page-open-leave-to {
  transform: translateX(-15%);
}

.page-open-enter-from {
  transform: translateX(15%);
  opacity: 0;
}

.page-open-enter-to {
  transform: translateX(0);
  opacity: 1;
}

.page-close-leave-active {
  z-index: 2;
}

.page-close-enter-active {
  z-index: 1;
}

.page-close-leave-from {
  transform: scale(1);
  opacity: 1;
}

.page-close-leave-to {
  transform: scale(0.95);
  opacity: 0;
}

.page-close-enter-from {
  transform: translateX(-15%);
}

.page-close-enter-to {
  transform: translateX(0);
}

.fade-through-enter-active,
.fade-through-leave-active {
  position: absolute;
  width: 100%;
  top: var(--window-inset-top, 0px);
  bottom: 0;
  left: 0;
  background-color: var(--color-background);
}

.fade-through-leave-active {
  transition: opacity var(--m3-spring-fast-effects-duration) var(--m3-spring-fast-effects);
  z-index: 1;
}

.fade-through-enter-active {
  transition:
    opacity var(--m3-spring-default-effects-duration) var(--m3-spring-default-effects) var(--m3-spring-fast-effects-duration),
    transform var(--m3-spring-default-spatial-duration) var(--m3-spring-default-spatial) var(--m3-spring-fast-effects-duration);
  z-index: 2;
}

.fade-through-leave-to {
  opacity: 0;
}

.fade-through-enter-from {
  opacity: 0;
  transform: scale(0.96);
}

@media (prefers-reduced-motion: reduce) {
  .page-open-enter-active,
  .page-open-leave-active,
  .page-close-enter-active,
  .page-close-leave-active,
  .fade-through-enter-active,
  .fade-through-leave-active {
    transition: none;
  }
}
</style>