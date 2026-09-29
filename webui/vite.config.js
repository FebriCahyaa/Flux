import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
//import vueDevTools from 'vite-plugin-vue-devtools'
import tailwindcss from '@tailwindcss/vite'
import mkcert from "vite-plugin-mkcert";
import { ViteMinifyPlugin } from 'vite-plugin-minify';

// https://vite.dev/config/
export default defineConfig({
  // The build output (dist/) is copied verbatim into module/webroot/ and served
  // by whichever root manager hosts the module's WebUI, never from a domain
  // root. Vite's default base ('/') emits absolute "/assets/..." references
  // that 404 under that layout; relative references resolve correctly
  // regardless of the mount path the host uses.
  base: './',
  plugins: [
    vue(),
    //vueDevTools(),
    tailwindcss(),
    ViteMinifyPlugin({}),
    mkcert(),
  ],
  server : {
    https: true,
  },
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url))
    },
  },
})
