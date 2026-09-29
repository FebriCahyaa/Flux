<template>
  <svg :width="size" :height="size" viewBox="0 0 24 24" fill="none">
    <!-- Broom / sweep icon — Material Symbols "cleaning_services" simplified -->
    <path
      opacity="0.4"
      d="M16 3L21 8L14 15H10V11L16 3Z"
      fill="currentColor"
    />
    <path
      d="M14 15H10V11L16 3L21 8L14 15ZM3 21C3 18.24 5.24 16 8 16H10V15H14V16C14 16 15 16 15 17L10 22H8C5.24 22 3 19.76 3 17V21Z"
      fill="currentColor"
    />
    <path
      d="M3 21V17C3 14.79 4.79 13 7 13H10V15H8C6.34 15 5 16.34 5 18V21H3Z"
      fill="currentColor"
      opacity="0.6"
    />
  </svg>
</template>

<script setup>
defineProps({
  size: { type: [String, Number], default: 24 },
})
</script>
