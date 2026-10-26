import { defineConfig, devices } from "@playwright/test";

// The built page (npm run build) served by `vite preview`, in headless Chromium and WebKit; or, with
// OO1_BASE_URL (the page's URL), the page where it is published, and no server of our own. It is a
// directory: the specs go to "./".
const published = process.env.OO1_BASE_URL?.replace(/\/?$/, "/");

export default defineConfig({
  testDir: "e2e",
  timeout: 30_000,
  fullyParallel: true,
  reporter: [["list"]],
  use: { baseURL: published ?? "http://127.0.0.1:4173/", viewport: { width: 1280, height: 860 } },
  // Chromium, and WebKit for Safari on the Mac: they lay out the CRT differently enough to matter.
  projects: [
    { name: "chromium", use: { ...devices["Desktop Chrome"], viewport: { width: 1280, height: 860 } } },
    { name: "webkit", use: { ...devices["Desktop Safari"], viewport: { width: 1440, height: 800 }, deviceScaleFactor: 2 } }, // a Retina Mac
  ],
  webServer: published ? undefined : { command: "npx vite preview --host 127.0.0.1 --port 4173 --strictPort", url: "http://127.0.0.1:4173/", reuseExistingServer: false },
});
