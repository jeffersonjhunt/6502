// Put the emulator core (oo1.mjs + oo1.wasm, built by emulator/tools/web_build.sh)
// in public/core, where the page loads it. OO1_CORE_DIR overrides where to find it.
// A missing core is an error, never a page that silently has no machine.
import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ui = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const from = resolve(process.env.OO1_CORE_DIR ?? join(ui, "../../../build/web/apps/web"));
const to = join(ui, "public/core");
const files = ["oo1.mjs", "oo1.wasm"];
const missing = files.filter((f) => !existsSync(join(from, f)));
if (missing.length) {
  console.error(`copy-core: ${missing.join(", ")} not in ${from}; build it with emulator/tools/web_build.sh`);
  process.exit(1);
}
mkdirSync(to, { recursive: true });
for (const f of files) copyFileSync(join(from, f), join(to, f));
console.log(`copy-core: ${from} -> public/core`);
