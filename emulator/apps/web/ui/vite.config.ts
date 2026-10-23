import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { defineConfig } from "vitest/config";

// base "./": the built page works from any directory (dist/ as plain files).
// The emulator core is not bundled: scripts/copy-core.mjs puts oo1.mjs and
// oo1.wasm in public/core, and the page imports them at run time.
//
// Those two keep their names, so the page asks for them with ?v=<hash of
// their bytes>: a new core is a new URL, never a stale copy from a cache.
// VITE_HOME_URL, if set, makes the badge a link back to where the page is
// published from (the OO-1 project page on oneoffendeavors.com).
function coreVersion(): string {
  const files = ["public/core/oo1.mjs", "public/core/oo1.wasm"];
  if (!files.every((f) => existsSync(f))) return "none";
  const hash = createHash("sha256");
  for (const f of files) hash.update(readFileSync(f));
  return hash.digest("hex").slice(0, 12);
}

export default defineConfig({
  base: "./",
  define: { "import.meta.env.VITE_CORE_VERSION": JSON.stringify(coreVersion()) },
  build: { target: "es2022" },
  test: {
    include: ["src/**/*.test.ts"],
    environment: "node",
  },
});
