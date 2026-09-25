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
    <rect x="2.5" y="6" width="16" height="11" rx="2.5" />
    <circle cx="10.5" cy="11.5" r="2.8" />
    <path d="M18.5 9h3M18.5 14h3" />
    <path d="M6 17v3M9.5 17v3" />
  </svg>
</template>

<script setup>
defineProps({
  size: { type: [String, Number], default: 24 },
  color: { type: String, default: 'currentColor' },
})
</script>
