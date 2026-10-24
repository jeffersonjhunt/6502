/**
 * The owner's Apple IIe ROM, from files they pick to the cartridge, and
 * kept in this browser (localStorage) so it is there next time. It goes
 * nowhere else: the page never ships Apple's ROMs, and never uploads them.
 *
 * Files are told apart by size and contents, not names (dumps come named
 * every way): 16 KB is the whole $C000-$FFFF; two 8 KB halves are CD and
 * EF, EF being the one with the IIe's ID byte ($FBB3 = $06) and a reset
 * vector into ROM; 4 KB (or a third 8 KB) is the character ROM.
 */

import type { Cartridge } from "../port/machine-port";
import { prefs } from "./dom";

export interface RomFile {
  name: string;
  bytes: Uint8Array;
}

const K = 1024;
const ID = 0xfbb3 - 0xc000; // in a 16 KB image
const hex2 = (v: number) => v.toString(16).toUpperCase().padStart(2, "0");

/** An 8 KB $E000-$FFFF half: the IIe's ID byte, and a reset vector into ROM. */
function isEf(b: Uint8Array): boolean {
  return b[0x1bb3] === 0x06 && b[0x1ffd] >= 0xc0;
}

/** The files someone picked, as a cartridge; or why not. */
export function sortRoms(files: RomFile[]): Cartridge | string {
  let rom: Uint8Array | null = null;
  let chars: Uint8Array | null = null;
  const halves: RomFile[] = [];
  for (const f of files) {
    const n = f.bytes.length;
    if (n === 16 * K) {
      if (rom) return `${f.name}: two 16 KB ROMs; pick one`;
      rom = f.bytes;
    } else if (n === 4 * K) {
      if (chars) return `${f.name}: two character ROMs; pick one`;
      chars = f.bytes;
    } else if (n === 8 * K) halves.push(f);
    else return `${f.name}: ${n} bytes is no IIe ROM (want 16 KB, two 8 KB halves, and a 4 KB character ROM)`;
  }
  const ef = halves.find((f) => isEf(f.bytes));
  if (!rom && ef) {
    const cd = halves.find((f) => f !== ef);
    if (!cd) return `${ef.name}: the $E000-$FFFF half; pick the $C000-$DFFF half with it`;
    rom = new Uint8Array(16 * K);
    rom.set(cd.bytes, 0);
    rom.set(ef.bytes, 8 * K);
    halves.splice(halves.indexOf(cd), 1);
    halves.splice(halves.indexOf(ef), 1);
  }
  for (const f of halves) {
    if (chars) return `${f.name}: one 8 KB file too many`;
    chars = f.bytes; // an 8 KB character ROM (the US/German 341-0161)
  }
  if (!rom) return "No IIe ROM among these: want 16 KB ($C000-$FFFF) or the CD and EF halves";
  if (rom[ID] !== 0x06) return `Not an Apple IIe ROM: $FBB3 is $${hex2(rom[ID])}, a IIe's is $06`;
  return { rom, chars };
}

function toBase64(b: Uint8Array): string {
  let s = "";
  for (const c of b) s += String.fromCharCode(c);
  return btoa(s);
}

function fromBase64(s: string): Uint8Array | null {
  try {
    return Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
  } catch {
    return null;
  }
}

/** Keep it for next time (in this browser only); false if the browser would not. */
export function keepCartridge(c: Cartridge): boolean {
  const rom = toBase64(c.rom);
  prefs.set("apple-rom", rom);
  prefs.set("apple-chars", c.chars ? toBase64(c.chars) : "");
  return prefs.get("apple-rom") === rom;
}

export function keptCartridge(): Cartridge | null {
  const rom = fromBase64(prefs.get("apple-rom") ?? "");
  if (!rom || rom.length !== 16 * K) return null;
  const chars = fromBase64(prefs.get("apple-chars") ?? "");
  return { rom, chars: chars && chars.length ? chars : null };
}

export function forgetCartridge(): void {
  prefs.remove("apple-rom");
  prefs.remove("apple-chars");
}
