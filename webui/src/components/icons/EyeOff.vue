<template>
  <svg
    :width="size"
    :height="size"
    viewBox="0 0 24 24"
    fill="none"
    :stroke="color"
    stroke-width="2"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >
    <path d="M3 3l18 18" />
    <path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c5 0 8.6 3.6 10 7-.5 1.2-1.3 2.5-2.3 3.6" />
    <path d="M6.6 6.6C4.4 8 2.8 10 2 12c1.4 3.4 5 7 10 7 1.9 0 3.6-.5 5.1-1.4" />
    <path d="M9.9 9.9a3 3 0 0 0 4.2 4.2" />
  </svg>
</template>

<script setup>
defineProps({
  size: { type: [String, Number], default: 24 },
  color: { type: String, default: 'currentColor' },
})
</script>
