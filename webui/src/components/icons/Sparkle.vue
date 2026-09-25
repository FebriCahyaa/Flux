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
    <path d="M11 3.5l1.9 4.8 4.8 1.9-4.8 1.9L11 16.9l-1.9-4.8-4.8-1.9 4.8-1.9z" :fill="color" />
    <path d="M18.5 15v5M16 17.5h5" />
  </svg>
</template>

<script setup>
defineProps({
  size: { type: [String, Number], default: 24 },
  color: { type: String, default: 'currentColor' },
})
</script>
