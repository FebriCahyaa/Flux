// Renders the Flux artwork with Chromium (Playwright): banner HTML -> PNG.
// Encoding to AVIF / WebP is done by art/encode.py. Development tool, not part of the build.
//   node art/render.js <out dir>
const { chromium } = require(process.env.PLAYWRIGHT || 'playwright')
const path = require('path')
const out = process.argv[2] || path.join(__dirname, 'out')
require('fs').mkdirSync(out, { recursive: true })
;(async () => {
  const browser = await chromium.launch()
  const jobs = [
    ['banner.html', 'banner.png', 1280, 640, false],
  ]
  for (const [src, dst, w, h, transparent] of jobs) {
    if (!require('fs').existsSync(path.join(__dirname, src))) continue
    const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 })
    await page.goto('file://' + path.join(__dirname, src))
    await page.waitForTimeout(300)
    await page.screenshot({ path: path.join(out, dst), omitBackground: transparent })
    await page.close()
    console.log('rendered', dst)
  }
  await browser.close()
})()
