import { defineConfig, devices } from "@playwright/test";

// The built page (npm run build) served by `vite preview`, in headless Chromium.
export default defineConfig({
  testDir: "e2e",
  timeout: 30_000,
  fullyParallel: true,
  reporter: [["list"]],
  use: { baseURL: "http://127.0.0.1:4173/", ...devices["Desktop Chrome"], viewport: { width: 1280, height: 860 } },
  webServer: { command: "npx vite preview --host 127.0.0.1 --port 4173 --strictPort", url: "http://127.0.0.1:4173/", reuseExistingServer: false },
});
