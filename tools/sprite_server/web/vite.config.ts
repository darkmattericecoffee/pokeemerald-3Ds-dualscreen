/// <reference types="vitest/config" />
import path from "node:path";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// `npm run dev` serves the app on :5173 and sends the API to a local server.py (PORT 8787 by
// default, SERVER=http://host:port to change). `npm run build` writes dist/, which server.py serves.
const server = process.env.SERVER ?? "http://127.0.0.1:8787";

export default defineConfig({
  plugins: [react(), tailwindcss()],
  resolve: { alias: { "@": path.resolve(__dirname, "src") } },
  server: {
    proxy: {
      "/api": { target: server, changeOrigin: false },
      "/img": { target: server },
      "/dl": { target: server },
    },
  },
  // three.js (about 900 kB) is split off by the lazy 3D viewer and loads only on pages with 3D.
  build: { chunkSizeWarningLimit: 1000 },
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: ["src/test/setup.ts"],
    css: false,
  },
});
