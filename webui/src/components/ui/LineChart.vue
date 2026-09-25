<template>
  <!--
    Small SVG line chart. Several series share one y-axis; null values break a
    line. Optional shaded x-ranges (e.g. seconds in Performance Lite) and a
    dashed reference line (e.g. the FPS drop line).
  -->
  <svg
    class="line-chart"
    :viewBox="`0 0 ${W} ${height}`"
    preserveAspectRatio="none"
    role="img"
    :aria-label="label"
  >
    <rect
      v-for="(r, i) in shadeRects"
      :key="`s${i}`"
      :x="r.x"
      y="0"
      :width="r.w"
      :height="height"
      class="shade"
    />
    <g v-for="g in grid" :key="`g${g.v}`">
      <line x1="0" :x2="W" :y1="g.y" :y2="g.y" class="grid" />
      <text x="4" :y="g.y + 11" class="tick">{{ g.v }}{{ unit }}</text>
    </g>
    <line v-if="refY !== null" x1="0" :x2="W" :y1="refY" :y2="refY" class="ref" />
    <template v-for="(s, i) in paths" :key="`p${i}`">
      <path v-if="s.area" :d="s.area" class="area" :style="{ fill: s.color }" />
      <path
        :d="s.line"
        class="line"
        :style="{ stroke: s.color, strokeWidth: s.width, opacity: s.opacity }"
      />
    </template>
  </svg>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  // [{ values: number[] (null = gap), color: css colour, area?: bool, width?: number, opacity?: number }]
  series: { type: Array, required: true },
  height: { type: Number, default: 120 },
  min: { type: Number, default: null },
  max: { type: Number, default: null },
  unit: { type: String, default: '' },
  shade: { type: Array, default: () => [] }, // booleans per point
  reference: { type: Number, default: null },
  label: { type: String, default: '' },
})

const W = 300
const PAD = 8

const range = computed(() => {
  const all = props.series
    .flatMap((s) => s.values)
    .filter((v) => v !== null && v !== undefined && !Number.isNaN(v))
  let lo = props.min ?? (all.length ? Math.min(...all) : 0)
  let hi = props.max ?? (all.length ? Math.max(...all) : 1)
  if (props.min === null) lo = Math.floor(lo / 10) * 10 - 5
  if (props.max === null) hi = Math.ceil(hi / 10) * 10 + 5
  if (hi - lo < 10) hi = lo + 10
  return { lo: Math.max(props.min ?? -Infinity, lo), hi }
})

const count = computed(() => Math.max(2, ...props.series.map((s) => s.values.length)))
const x = (i) => (count.value <= 1 ? W : (i * W) / (count.value - 1))
const y = (v) => {
  const { lo, hi } = range.value
  return PAD + (1 - (v - lo) / (hi - lo)) * (props.height - 2 * PAD)
}

const paths = computed(() =>
  props.series.map((s) => {
    let line = ''
    let area = ''
    let run = []
    const flush = () => {
      if (run.length > 1 && s.area) {
        area +=
          `M${run[0][0]},${props.height} ` +
          run.map(([px, py]) => `L${px},${py}`).join(' ') +
          ` L${run[run.length - 1][0]},${props.height} Z `
      }
      run = []
    }
    s.values.forEach((v, i) => {
      if (v === null || v === undefined || Number.isNaN(v)) {
        flush()
        return
      }
      const pt = [x(i).toFixed(1), y(v).toFixed(1)]
      line += `${run.length ? 'L' : 'M'}${pt[0]},${pt[1]} `
      run.push(pt)
    })
    flush()
    return {
      line,
      area: s.area ? area : '',
      color: s.color,
      width: s.width ?? 2,
      opacity: s.opacity ?? 1,
    }
  }),
)

const grid = computed(() => {
  const { lo, hi } = range.value
  const step = (hi - lo) / 3
  return [1, 2].map((k) => {
    const v = Math.round(lo + step * k)
    return { v, y: y(v) }
  })
})

const refY = computed(() => (props.reference === null ? null : y(props.reference)))

const shadeRects = computed(() => {
  const rects = []
  const n = props.shade.length
  if (n < 2) return rects
  let start = -1
  for (let i = 0; i <= n; i++) {
    if (i < n && props.shade[i]) {
      if (start < 0) start = i
    } else if (start >= 0) {
      const x0 = (start * W) / (n - 1)
      const x1 = ((i - 1) * W) / (n - 1)
      rects.push({ x: x0, w: Math.max(2, x1 - x0) })
      start = -1
    }
  }
  return rects
})
</script>

<style scoped>
.line-chart {
  display: block;
  width: 100%;
  overflow: visible;
}

.line {
  fill: none;
  stroke-linecap: round;
  stroke-linejoin: round;
  vector-effect: non-scaling-stroke;
}

.area {
  opacity: 0.14;
}

.grid {
  stroke: var(--color-outline-variant);
  stroke-width: 1;
  vector-effect: non-scaling-stroke;
  opacity: 0.6;
}

.ref {
  stroke: var(--color-error);
  stroke-width: 1.5;
  stroke-dasharray: 4 4;
  vector-effect: non-scaling-stroke;
  opacity: 0.7;
}

.tick {
  fill: var(--color-on-surface-variant);
  font-size: 9px;
  opacity: 0.8;
}

.shade {
  fill: var(--color-tertiary);
  opacity: 0.12;
}
</style>
