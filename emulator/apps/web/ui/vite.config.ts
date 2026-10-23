import { defineConfig } from "vitest/config";

// base "./": the built page works from any directory (dist/ as plain files).
// The emulator core is not bundled: scripts/copy-core.mjs puts oo1.mjs and
// oo1.wasm in public/core, and the page imports them at run time.
export default defineConfig({
  base: "./",
  build: { target: "es2022" },
  test: {
    include: ["src/**/*.test.ts"],
    environment: "node",
  },
});
