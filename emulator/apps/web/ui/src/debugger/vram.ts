/**
 * Card memory, under the Devices tab's video card: the WebUI's `vram`, as
 * the terminal app's debugger has it (hw/video-rev2.md, "Emulator
 * changes"). A revision 2 card draws from 2 MB of its own memory, which the
 * CPU reaches only through the aperture's slots; this shows and writes it
 * directly, with no bus cycle and the card's port untouched.
 *
 * One line, the terminal's syntax: `ADDR [N]` shows N bytes (decimal,
 * 1-4096; here 128 by default) from a 24-bit hex card address, `ADDR =
 * BYTE...` writes the bytes there. The dump follows the machine as it runs.
 */

import type { MachinePort } from "../port/machine-port";
import { h } from "../ui/dom";
import { hex } from "./breakpoints";

export const VRAM_DEFAULT = 128;
const MAX = 4096;
const PER_ROW = 8; // the drawer is narrow

export type VramLine = { addr: number; count: number; bytes?: Uint8Array };

const hexArg = (s: string, digits: number): number | null => {
  const m = new RegExp(`^\\$?([0-9a-f]{1,${digits}})$`, "i").exec(s);
  return m ? parseInt(m[1], 16) : null;
};

/** A `vram` line: what to show, and what to write first; or why not. */
export function parseVram(text: string): VramLine | string {
  const w = text.trim().split(/\s+/).filter(Boolean);
  const usage = "ADDR [N], or ADDR = BYTE...: ADDR is a card address, up to 6 hex digits; N is decimal, 1-4096";
  const addr = w.length ? hexArg(w[0], 6) : null;
  if (addr === null) return usage;
  if (w[1] === "=") {
    if (w.length === 2) return usage;
    const bytes: number[] = [];
    for (const b of w.slice(2)) {
      const v = hexArg(b, 2);
      if (v === null) return `bad byte '${b}'`;
      bytes.push(v);
    }
    return { addr, count: Math.max(bytes.length, VRAM_DEFAULT), bytes: Uint8Array.from(bytes) };
  }
  if (w.length > 2) return usage;
  if (w.length === 1) return { addr, count: VRAM_DEFAULT };
  if (!/^\d+$/.test(w[1])) return usage;
  const count = Number(w[1]);
  return count >= 1 && count <= MAX ? { addr, count } : usage;
}

/** Bytes as the terminal's `vram` shows them: the address, the bytes, and what prints. */
export function vramDump(addr: number, bytes: Uint8Array, perRow = PER_ROW): string {
  const lines: string[] = [];
  for (let row = 0; row < bytes.length; row += perRow) {
    const part = bytes.subarray(row, row + perRow);
    const hx = Array.from(part, (b) => hex(b, 2)).join(" ");
    const text = Array.from(part, (b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : ".")).join("");
    lines.push(`${hex((addr + row) & 0xffffff, 6)}  ${hx.padEnd(perRow * 3 - 1)}  ${text}`);
  }
  return lines.join("\n");
}

export function vramView(port: MachinePort) {
  const input = h("input", {
    type: "text",
    placeholder: "2000 64, or 1E1000 = 12 34",
    "aria-label": "Card memory: address and count, or address = bytes",
    spellcheck: "false",
    autocomplete: "off",
    "data-testid": "vram-input",
  });
  const show = h("button", { class: "tbtn", type: "submit", "data-testid": "vram-go" }, "Show");
  const form = h("form", { class: "bp-add" }, input, show);
  const error = h("p", { class: "bp-error", role: "alert", "data-testid": "vram-error" });
  const dump = h("pre", { class: "vram", "data-testid": "vram-dump" });
  const element = h("div", { class: "vram-box" }, form, error, dump);

  let shown: { addr: number; count: number } = { addr: 0x2000, count: VRAM_DEFAULT }; // the console's text

  const refresh = async () => {
    const bytes = await port.readCardMemory(shown.addr, shown.count);
    form.hidden = !bytes;
    const text = bytes ? vramDump(shown.addr, bytes) : "This video card has no memory of its own.";
    if (dump.textContent !== text) dump.textContent = text; // unchanged: keep the node, and a selection in it
  };

  form.addEventListener("submit", (e) => {
    e.preventDefault();
    const r = parseVram(input.value);
    if (typeof r === "string") {
      error.textContent = r;
      return;
    }
    error.textContent = "";
    if (r.bytes) port.writeCardMemory(r.addr, r.bytes);
    shown = { addr: r.addr, count: r.count };
    void refresh();
  });

  return { element, refresh };
}
