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
    <path d="M2.5 8.8a14 14 0 0 1 19 0" />
    <path d="M5.8 12.4a9.2 9.2 0 0 1 12.4 0" />
    <path d="M9.1 16a4.4 4.4 0 0 1 5.8 0" />
    <circle cx="12" cy="19.3" r="1.2" :fill="color" stroke="none" />
  </svg>
</template>

<script setup>
defineProps({
  size: { type: [String, Number], default: 24 },
  color: { type: String, default: 'currentColor' },
})
</script>
