<template>
  <svg
    xmlns="http://www.w3.org/2000/svg"
    :width="size"
    :height="size"
    viewBox="0 -960 960 960"
    :fill="color"
  >
    <path
      v-if="active"
      d="M120-200v-80h80v-480h560v480h80v80H120Zm240-160h80v-240h-80v240Zm120 0h80v-400h-80v400Zm120 0h80v-160h-80v160Z"
    />
    <path
      v-else
      d="M120-200v-80h80v-480h560v480h80v80H120Zm160-80h400v-400H280v400Zm120-80h80v-240h-80v240Zm120 0h80v-400h-80v400Zm-120-120Zm120 0Z"
    />
  </svg>
</template>

<script setup>
const props = defineProps({
  size: {
    type: [String, Number],
    default: 24,
  },
  color: {
    type: String,
    default: 'currentColor',
  },
  active: {
    type: Boolean,
    default: false,
  },
})
</script>
