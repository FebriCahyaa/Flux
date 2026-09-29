<template>
  <svg :width="size" :height="size" viewBox="0 0 24 24" fill="none">
    <!-- Memory/RAM chip — Material Symbols "memory" path -->
    <path
      opacity="0.4"
      d="M4 8C4 5.79 5.79 4 8 4H16C18.21 4 20 5.79 20 8V16C20 18.21 18.21 20 16 20H8C5.79 20 4 18.21 4 16V8Z"
      fill="currentColor"
    />
    <path
      d="M9 2V4H7V2H9ZM13 2V4H11V2H13ZM17 2V4H15V2H17ZM22 9H20V7H22V9ZM22 13H20V11H22V13ZM22 17H20V15H22V17ZM15 22V20H17V22H15ZM11 22V20H13V22H11ZM7 22V20H9V22H7ZM2 15H4V17H2V15ZM2 11H4V13H2V11ZM2 7H4V9H2V7ZM9 8H15C15.55 8 16 8.45 16 9V15C16 15.55 15.55 16 15 16H9C8.45 16 8 15.55 8 15V9C8 8.45 8.45 8 9 8ZM10 10V14H14V10H10Z"
      fill="currentColor"
    />
  </svg>
</template>

<script setup>
defineProps({
  size: { type: [String, Number], default: 24 },
})
</script>
