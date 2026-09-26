// Flux Tweaks website: language, theme and the module store.
// The store reads modules.json, then each module's own update JSON (the same file
// Magisk / KernelSU read) or its latest GitHub release, so versions and download
// links are always the current ones without editing this site.

const I18N = {
  id: {
    'nav.skip': 'Langsung ke store',
    'nav.features': 'Fitur',
    'nav.store': 'Store',
    'nav.install': 'Instal',
    'hero.tag': 'Performance, evolved.',
    'hero.sub':
      'Modul optimasi game dan baterai untuk Android yang di-root. Profil menyesuaikan sendiri saat game berjalan, saat HP panas dan saat dipakai harian.',
    'hero.download': 'Unduh Flux',
    'hero.store': 'Lihat semua modul',
    'hero.card.label': 'Profil otomatis',
    'hero.card.game': 'Game dibuka',
    'hero.card.hot': 'HP panas',
    'hero.card.daily': 'Pemakaian harian',
    'hero.card.saver': 'Hemat baterai',
    'features.title': 'Yang dilakukan Flux',
    'features.lead':
      'Setiap perubahan disimpan dulu lalu dikembalikan persis seperti semula begitu kamu keluar dari game.',
    'f1.t': 'Profil yang mengenali game',
    'f1.d': 'Performance saat game terbuka, Balance saat dipakai biasa, Powersave saat hemat baterai. Pindah sendiri tanpa perlu dibuka.',
    'f2.t': 'Thread render di core cepat',
    'f2.d': 'Thread render Unity, Unreal dan GL dipindah ke core tercepat dengan prioritas tinggi. Susunan core dibaca dari kernel, jadi cocok untuk 4+4, 1+3+4 maupun 2+6.',
    'f3.t': 'Performance Lite saat panas',
    'f3.d': 'Membaca sisa ruang termal Android, status thermal dan suhu CPU. Saat HP terlalu panas, profil turun ke Lite lalu naik lagi setelah dingin.',
    'f4.t': 'Refresh rate mengikuti game',
    'f4.d': 'Memilih mode layar terkecil yang masih menampilkan semua frame game, misalnya 90 Hz untuk game 90 FPS di panel 120 Hz.',
    'f5.t': 'Aturan per chipset',
    'f5.d': 'Chip yang dikenal cepat panas (Snapdragon 888, 8 Gen 1, Parrot, Exynos 2100/2200, Dimensity 9000) otomatis mendapat batas yang lebih aman.',
    'f6.t': 'Statistik sesi',
    'f6.d': 'FPS rata-rata, 1% low, drop, stabilitas dan suhu dicatat per sesi dari frame game itu sendiri, lengkap dengan grafik di WebUI.',
    'store.title': 'Store',
    'store.lead': 'Modul dan komponen buatan FebriCahyaa. Versi dan link unduhan diambil langsung dari rilis terbaru.',
    'store.all': 'Semua',
    'store.modules': 'Modul',
    'store.components': 'Komponen',
    'store.download': 'Unduh',
    'store.bundled': 'Sudah termasuk di Flux',
    'store.requires': 'Butuh',
    'store.source': 'Kode sumber',
    'store.releases': 'Rilis',
    'store.changelog': 'Changelog',
    'store.open': 'Open source',
    'store.private': 'Privat',
    'store.component': 'Komponen',
    'store.unavailable': 'Versi tidak terbaca',
    'store.copied': 'SHA-256 disalin',
    'store.sha': 'Salin SHA-256',
    'store.error': 'Daftar modul gagal dimuat. Coba muat ulang halaman.',
    'install.title': 'Cara memasang',
    's1.t': 'Unduh zip',
    's1.d': 'Pilih Universal kalau ragu. arm64 untuk ROM 64-bit, arm untuk ROM 32-bit.',
    's2.t': 'Pasang di root manager',
    's2.d': 'Magisk, KernelSU / KernelSU-Next atau APatch: Modul → Instal dari penyimpanan.',
    's3.t': 'Reboot dan buka WebUI',
    's3.d': 'Atur game, profil dan tweak dari WebUI modul. HiCo Thermal dipasang setelah Flux.',
    'footer.note': 'Flux Tweaks dan SynthesisCore berlisensi Apache-2.0. HiCo Thermal adalah perangkat lunak privat di bawah EULA.',
  },
  en: {
    'nav.skip': 'Skip to the store',
    'nav.features': 'Features',
    'nav.store': 'Store',
    'nav.install': 'Install',
    'hero.tag': 'Performance, evolved.',
    'hero.sub':
      'Gaming and battery optimization for rooted Android. Profiles adapt on their own while you play, when the phone runs hot and in daily use.',
    'hero.download': 'Download Flux',
    'hero.store': 'See all modules',
    'hero.card.label': 'Automatic profiles',
    'hero.card.game': 'Game opened',
    'hero.card.hot': 'Phone hot',
    'hero.card.daily': 'Daily use',
    'hero.card.saver': 'Battery saver',
    'features.title': 'What Flux does',
    'features.lead': 'Every change is saved first and put back exactly as it was the moment you leave the game.',
    'f1.t': 'Game-aware profiles',
    'f1.d': 'Performance while a game is open, Balance in daily use, Powersave with battery saver. Switches on its own, nothing to open.',
    'f2.t': 'Render threads on fast cores',
    'f2.d': 'Unity, Unreal and GL render threads move to the fastest cores at high priority. The core layout is read from the kernel, so it fits 4+4, 1+3+4 and 2+6 alike.',
    'f3.t': 'Performance Lite when hot',
    'f3.d': "Reads Android's thermal headroom, the thermal status and the CPU temperature. When the phone gets too hot the profile drops to Lite and comes back once it cools.",
    'f4.t': 'Refresh rate follows the game',
    'f4.d': 'Picks the smallest display mode that still shows every frame, such as 90 Hz for a 90 FPS game on a 120 Hz panel.',
    'f5.t': 'Per-chipset rules',
    'f5.d': 'Chips known to run hot (Snapdragon 888, 8 Gen 1, Parrot, Exynos 2100/2200, Dimensity 9000) get safer limits automatically.',
    'f6.t': 'Session statistics',
    'f6.d': "Average FPS, 1% low, drops, stability and temperatures per session, measured from the game's own frames, with charts in the WebUI.",
    'store.title': 'Store',
    'store.lead': 'Modules and components by FebriCahyaa. Versions and download links come straight from the latest releases.',
    'store.all': 'All',
    'store.modules': 'Modules',
    'store.components': 'Components',
    'store.download': 'Download',
    'store.bundled': 'Included in Flux',
    'store.requires': 'Requires',
    'store.source': 'Source code',
    'store.releases': 'Releases',
    'store.changelog': 'Changelog',
    'store.open': 'Open source',
    'store.private': 'Private',
    'store.component': 'Component',
    'store.unavailable': 'Version unavailable',
    'store.copied': 'SHA-256 copied',
    'store.sha': 'Copy SHA-256',
    'store.error': 'The module list could not be loaded. Try reloading the page.',
    'install.title': 'How to install',
    's1.t': 'Download the zip',
    's1.d': 'Pick Universal when unsure. arm64 for 64-bit ROMs, arm for 32-bit ROMs.',
    's2.t': 'Flash it in your root manager',
    's2.d': 'Magisk, KernelSU / KernelSU-Next or APatch: Modules → Install from storage.',
    's3.t': 'Reboot and open the WebUI',
    's3.d': "Set up games, profiles and tweaks in the module's WebUI. Install HiCo Thermal after Flux.",
    'footer.note': 'Flux Tweaks and SynthesisCore are Apache-2.0. HiCo Thermal is private software under its EULA.',
  },
}

const store = {
  get(key) {
    try {
      return localStorage.getItem(key)
    } catch {
      return null
    }
  },
  set(key, value) {
    try {
      localStorage.setItem(key, value)
    } catch {}
  },
}

// Saved choice, else Indonesian for Indonesian browsers and English for the rest.
const savedLang = store.get('flux-lang')
let lang =
  savedLang === 'en' || savedLang === 'id'
    ? savedLang
    : (navigator.language || '').toLowerCase().startsWith('id')
      ? 'id'
      : 'en'
const t = (key) => I18N[lang][key] ?? I18N.en[key] ?? key

// Small DOM builder: text always goes through textContent, never HTML.
function h(tag, props = {}, ...children) {
  const el = document.createElement(tag)
  for (const [k, v] of Object.entries(props)) {
    if (v == null || v === false) continue
    if (k === 'class') el.className = v
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v)
    else el.setAttribute(k, v)
  }
  for (const c of children.flat()) {
    if (c == null || c === false) continue
    el.append(c instanceof Node ? c : document.createTextNode(String(c)))
  }
  return el
}
const SVG = 'http://www.w3.org/2000/svg'
function icon(name) {
  const svg = document.createElementNS(SVG, 'svg')
  svg.setAttribute('viewBox', '0 0 24 24')
  svg.setAttribute('aria-hidden', 'true')
  const use = document.createElementNS(SVG, 'use')
  use.setAttribute('href', `#i-${name}`)
  svg.append(use)
  return svg
}

// Only links to GitHub are followed from fetched data.
const safeUrl = (u) => {
  try {
    const url = new URL(u)
    return url.protocol === 'https:' && /(^|\.)github(usercontent)?\.com$/.test(url.hostname) ? url.href : null
  } catch {
    return null
  }
}

function toast(text) {
  document.querySelector('.toast')?.remove()
  const el = h('div', { class: 'toast', role: 'status' }, text)
  document.body.append(el)
  setTimeout(() => el.remove(), 2200)
}

// ── Language and theme ───────────────────────────────────────────────
function applyLang() {
  document.documentElement.lang = lang
  document.getElementById('lang-code').textContent = lang.toUpperCase()
  for (const el of document.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n)
  renderStore()
}
document.getElementById('lang').addEventListener('click', () => {
  lang = lang === 'id' ? 'en' : 'id'
  store.set('flux-lang', lang)
  applyLang()
})

function currentTheme() {
  const set = document.documentElement.dataset.theme
  if (set) return set
  return matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark'
}
function paintThemeColor() {
  document.querySelector('meta[name=theme-color]').content = currentTheme() === 'light' ? '#fbf8ff' : '#13102e'
}
document.getElementById('theme').addEventListener('click', () => {
  const next = currentTheme() === 'light' ? 'dark' : 'light'
  document.documentElement.dataset.theme = next
  store.set('flux-theme', next)
  paintThemeColor()
})
paintThemeColor()

// ── Store ────────────────────────────────────────────────────────────
let modules = null
let loadError = false
const releases = {} // id -> { version, flavors: {universal|arm64|arm: {url, sha256}}, changelog }
let filter = 'all'

async function getJson(url) {
  const res = await fetch(url, { cache: 'no-cache' })
  if (!res.ok) throw new Error(`${res.status} ${url}`)
  return res.json()
}

async function loadRelease(m) {
  if (m.update) {
    const entries = await Promise.all(
      Object.entries(m.update).map(async ([flavor, url]) => {
        try {
          return [flavor, await getJson(url)]
        } catch {
          return [flavor, null]
        }
      }),
    )
    const flavors = {}
    let version = null
    let changelog = null
    for (const [flavor, j] of entries) {
      if (!j) continue
      flavors[flavor] = { url: safeUrl(j.zipUrl), sha256: /^[0-9a-f]{64}$/i.test(j.sha256 || '') ? j.sha256 : null }
      if (flavor === 'universal' || !version) {
        version = typeof j.version === 'string' ? j.version : null
        changelog = safeUrl(j.changelog)
      }
    }
    return { version, flavors, changelog }
  }
  if (m.githubRelease && /^[\w.-]+\/[\w.-]+$/.test(m.githubRelease)) {
    const j = await getJson(`https://api.github.com/repos/${m.githubRelease}/releases/latest`)
    return { version: typeof j.tag_name === 'string' ? j.tag_name : null, flavors: {}, page: safeUrl(j.html_url) }
  }
  return { version: null, flavors: {} }
}

function renderStore() {
  const list = document.getElementById('store-list')
  if (loadError) {
    list.replaceChildren(h('p', { class: 'lead' }, t('store.error')))
    return
  }
  if (!modules) return
  const byId = Object.fromEntries(modules.map((m) => [m.id, m]))
  list.replaceChildren(
    ...modules.map((m) => {
      const rel = releases[m.id]
      const text = (o) => (o && (o[lang] || o.en)) || ''
      const licenseBadge =
        m.license?.type === 'private'
          ? h('span', { class: 'badge private' }, `${t('store.private')} · ${m.license.label}`)
          : h('span', { class: 'badge open' }, `${t('store.open')} · ${m.license?.label ?? ''}`)
      const versionBadge = rel
        ? rel.version
          ? h('span', { class: 'badge version' }, rel.version)
          : h('span', { class: 'badge warn' }, t('store.unavailable'))
        : null

      const actions = []
      if (m.kind === 'component') {
        actions.push(h('span', { class: 'badge' }, t('store.bundled')))
      } else if (rel?.flavors?.universal?.url) {
        actions.push(
          h('a', { class: 'btn btn-primary btn-sm', href: rel.flavors.universal.url }, icon('download'), t('store.download')),
        )
        const others = ['arm64', 'arm'].filter((f) => rel.flavors[f]?.url)
        if (others.length)
          actions.push(h('span', { class: 'flavors' }, others.map((f) => h('a', { class: 'flavor', href: rel.flavors[f].url }, f))))
      } else if (m.releases) {
        actions.push(h('a', { class: 'btn btn-tonal btn-sm', href: m.releases }, icon('open'), t('store.releases')))
      }

      const links = [
        m.repo && h('a', { href: m.repo }, icon('code'), t('store.source')),
        (rel?.page || m.releases) && h('a', { href: rel?.page || m.releases }, icon('open'), t('store.releases')),
        rel?.changelog && h('a', { href: rel.changelog }, icon('open'), t('store.changelog')),
      ]

      const sha = rel?.flavors?.universal?.sha256
      const requires = (m.requires || []).map((id) => byId[id]?.name || id)

      return h(
        'article',
        { class: 'card', 'data-kind': m.kind, hidden: filter !== 'all' && filter !== m.kind ? '' : null },
        h(
          'div',
          { class: 'card-top' },
          h('span', { class: `card-icon shape-${m.icon?.shape || 'cookie9'} t-${m.icon?.tone || 'primary'}` }, icon(m.icon?.glyph || 'bolt')),
          h('div', { class: 'card-title' }, h('h3', {}, m.name), h('p', {}, text(m.tagline))),
        ),
        h(
          'div',
          { class: 'badges' },
          versionBadge,
          licenseBadge,
          m.kind === 'component' && h('span', { class: 'badge' }, t('store.component')),
          (m.tags || []).map((tag) => h('span', { class: 'badge' }, tag)),
        ),
        h('p', { class: 'card-desc' }, text(m.description)),
        requires.length > 0 && h('p', { class: 'requires' }, `${t('store.requires')}: `, h('b', {}, requires.join(', '))),
        actions.length > 0 && h('div', { class: 'card-actions' }, actions),
        sha &&
          h(
            'button',
            {
              class: 'sha',
              type: 'button',
              title: t('store.sha'),
              onclick: () => navigator.clipboard?.writeText(sha).then(() => toast(t('store.copied'))),
            },
            icon('copy'),
            h('span', {}, `SHA-256 ${sha}`),
          ),
        h('div', { class: 'links' }, links),
      )
    }),
  )
}

for (const btn of document.querySelectorAll('.filter')) {
  btn.addEventListener('click', () => {
    filter = btn.dataset.filter
    for (const b of document.querySelectorAll('.filter')) b.classList.toggle('active', b === btn)
    renderStore()
  })
}

async function init() {
  document.getElementById('year').textContent = new Date().getFullYear()
  applyLang()
  try {
    modules = (await getJson('modules.json')).modules || []
  } catch {
    loadError = true
    renderStore()
    return
  }
  renderStore()
  await Promise.all(
    modules.map(async (m) => {
      try {
        releases[m.id] = await loadRelease(m)
      } catch {
        releases[m.id] = { version: null, flavors: {} }
      }
      renderStore()
    }),
  )
  // Hero button: Flux's universal zip and its version.
  const flux = releases.flux
  if (flux?.flavors?.universal?.url) document.getElementById('hero-download').href = flux.flavors.universal.url
  if (flux?.version) document.getElementById('hero-version').textContent = flux.version
}
init()
