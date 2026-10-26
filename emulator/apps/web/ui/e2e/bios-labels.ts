import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

// The BIOS's labels, from the same build's bios.rom.lbl that the page embeds
// (ld65's "al 00C9BA .ECHO"), so the specs follow the BIOS when its code moves.
// Run against a published page, it must be the build that was published.

const file = fileURLToPath(new URL("../../../../build/web/roms/bios/bios.rom.lbl", import.meta.url));
let labels: Map<string, number> | null = null;

/** A BIOS label's CPU address. */
export function bios(name: string): number {
  labels ??= new Map(
    readFileSync(file, "utf8")
      .split("\n")
      .map((l) => /^al ([0-9A-F]+) \.(\w+)$/i.exec(l.trim()))
      .filter((m): m is RegExpExecArray => !!m)
      .map((m) => [m[2], parseInt(m[1], 16)]),
  );
  const at = labels.get(name);
  if (at === undefined) throw new Error(`no BIOS label ${name} in ${file}`);
  return at;
}

/** "C9BA": an address as the debugger shows it. */
export const hex4 = (n: number) => n.toString(16).toUpperCase().padStart(4, "0");

/** The monitor's key loop, GETKEY up to TAKE: where PC is while it waits for a key. */
export function inKeyLoop(at: number): boolean {
  return at >= bios("GETKEY") && at < bios("TAKE");
}
