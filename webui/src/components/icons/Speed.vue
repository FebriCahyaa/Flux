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
    <path d="M4.2 17.5a8.5 8.5 0 1 1 15.6 0" />
    <path d="M12 14.5l4-4.5" />
    <circle cx="12" cy="14.5" r="1.6" :fill="color" stroke="none" />
    <path d="M7 17.5h10" />
  </svg>
</template>

<script setup>
defineProps({
  size: { type: [String, Number], default: 24 },
  color: { type: String, default: 'currentColor' },
})
</script>
