<template>
  <!--
    M3 Expressive loading indicator: a filled shape that morphs through the
    expressive shape set (cookie, pentagon, burst, clover, ...) while rotating.
    Shapes come from src/assets/expressive.css (tools/gen_expressive.py).
  -->
  <div class="flex justify-center items-center" role="progressbar" aria-busy="true">
    <div class="m3-loading" :style="{ width: `${size}px`, height: `${size}px`, '--c': color }">
      <span class="shape"></span>
    </div>
  </div>
</template>

<script setup>
defineProps({
  size: {
    type: [String, Number],
    default: 38,
  },
  color: {
    type: String,
    default: 'var(--color-primary)',
  },
})

defineOptions({
  name: 'LoadingSpinner',
})
</script>

<style scoped>
.m3-loading {
  display: grid;
  place-items: center;
}

.shape {
  width: 80%;
  height: 80%;
  background: var(--c);
  clip-path: var(--m3-shape-cookie9);
  animation:
    m3-loading-morph 4.2s var(--m3-spring-default-spatial) infinite,
    m3-loading-spin 4.2s linear infinite;
}

@keyframes m3-loading-spin {
  to {
    transform: rotate(1turn);
  }
}

@media (prefers-reduced-motion: reduce) {
  .shape {
    animation: m3-loading-spin 2.4s linear infinite;
  }
}
</style>
