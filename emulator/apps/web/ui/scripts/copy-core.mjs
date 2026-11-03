// Put the emulator core (oo1.mjs + oo1.wasm, built by emulator/tools/web_build.sh)
// in public/core, where the page loads it. OO1_CORE_DIR overrides where to find it.
// A missing core is an error, never a page that silently has no machine.
//
// And the demo card the page puts in the SD slot (emulator/test/host/sd/demo.sparse,
// made by oodos/tools/make_card.sh): there as demo-card.img.gz, the card's 32 MB
// whole, gzipped, for the page to unzip (DecompressionStream).
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { gzipSync } from "node:zlib";
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

/** A card from its sparse dump: "oo1-sparse 1", "sectors N", then "LBA hex" for each sector that isn't zeros. */
export function fromSparse(text) {
  const lines = text.split("\n");
  if (lines[0] !== "oo1-sparse 1" || !lines[1]?.startsWith("sectors ")) throw new Error("not an oo1-sparse 1 dump");
  const card = Buffer.alloc(Number(lines[1].slice(8)) * 512);
  for (const line of lines.slice(2)) {
    if (!line) continue;
    const [lba, hex] = line.split(" ");
    const sector = Buffer.from(hex, "hex");
    if (sector.length !== 512 || Number(lba) * 512 >= card.length) throw new Error(`a bad sector line: ${lba}`);
    sector.copy(card, Number(lba) * 512);
  }
  return card;
}

const sparse = join(ui, "../../../test/host/sd/demo.sparse");
if (!existsSync(sparse)) {
  console.error(`copy-core: no ${sparse}; make it with oodos/tools/make_card.sh`);
  process.exit(1);
}
writeFileSync(join(to, "demo-card.img.gz"), gzipSync(fromSparse(readFileSync(sparse, "utf8")), { level: 9 }));
console.log(`copy-core: ${sparse} -> public/core/demo-card.img.gz`);
