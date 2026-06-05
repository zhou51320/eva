import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { viteSingleFile } from 'vite-plugin-singlefile'

declare const process: { env: Record<string, string | undefined> }

// The ACP HTTP server (eva_acp) serves a single embedded index.html, so the whole
// app is inlined into one file. Build target is kept low enough to run on the last
// browsers available for Windows 7 (Chrome 109 / Firefox 115 ESR).
const LEGACY_TARGET = ['chrome87', 'firefox78', 'safari14', 'edge88']

// During `npm run dev`, proxy backend calls to a locally running eva_acp instance.
const ACP_DEV_TARGET = process.env.EVA_ACP_DEV_TARGET || 'http://127.0.0.1:19070'

export default defineConfig({
  plugins: [vue(), viteSingleFile()],
  base: './',
  build: {
    target: LEGACY_TARGET,
    cssTarget: LEGACY_TARGET,
    outDir: '../resource/acp_web',
    emptyOutDir: true,
    assetsInlineLimit: 100000000,
    chunkSizeWarningLimit: 100000,
    reportCompressedSize: false,
  },
  server: {
    port: 5174,
    proxy: {
      '/health': ACP_DEV_TARGET,
      '/api': ACP_DEV_TARGET,
      '/v1': ACP_DEV_TARGET,
    },
  },
})
