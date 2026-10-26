import type { Page } from "@playwright/test";

// The BIOS's labels from the page under test: the bios.rom.lbl embedded in
// the core it loaded (ld65's "al 00C9BA .ECHO"), so the specs follow the BIOS
// when its code moves, and match a published page (OO1_BASE_URL) whatever
// is built here. loadBiosLabels() once the page has booted, then bios().

let labels: Map<string, number> | null = null;

/** Read the labels out of the page's own core: the same file, a second instance. Once per run. */
export async function loadBiosLabels(page: Page): Promise<void> {
  if (labels) return;
  const text = await page.evaluate(async () => {
    const entry = performance.getEntriesByType("resource").find((e) => /\/core\/oo1\.mjs(\?|$)/.test(e.name));
    if (!entry) throw new Error("the page loaded no core/oo1.mjs");
    const url = new URL(entry.name);
    const at = (file: string) => {
      const u = new URL(file, url);
      u.search = url.search; // the build's version, as the page asks for it
      return u.href;
    };
    type Core = { FS: { readFile(path: string, options: { encoding: "utf8" }): string } };
    const mod = (await import(/* @vite-ignore */ url.href)) as { default: (o: { locateFile: (f: string) => string }) => Promise<Core> };
    return (await mod.default({ locateFile: at })).FS.readFile("/roms/bios.rom.lbl", { encoding: "utf8" });
  });
  labels = new Map(
    text
      .split("\n")
      .map((l) => /^al ([0-9A-F]+) \.(\w+)$/i.exec(l.trim()))
      .filter((m): m is RegExpExecArray => !!m)
      .map((m) => [m[2], parseInt(m[1], 16)]),
  );
}

/** A BIOS label's CPU address. */
export function bios(name: string): number {
  if (!labels) throw new Error("loadBiosLabels(page) first");
  const at = labels.get(name);
  if (at === undefined) throw new Error(`no BIOS label ${name} in the page's bios.rom.lbl`);
  return at;
}

/** "C9BA": an address as the debugger shows it. */
export const hex4 = (n: number) => n.toString(16).toUpperCase().padStart(4, "0");

/** The monitor's key loop, GETKEY up to TAKE: where PC is while it waits for a key. */
export function inKeyLoop(at: number): boolean {
  return at >= bios("GETKEY") && at < bios("TAKE");
}
