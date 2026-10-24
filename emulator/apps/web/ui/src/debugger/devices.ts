/**
 * The Devices tab: the cards' registers, decoded. So far the video card
 * (hw/design.md, "Video registers"), read at $C480 with readMemory, which
 * takes no bus cycle and changes nothing (ISR is read, not cleared). Bases
 * are shown as the physical addresses they point at: $80000 + base x 1 KB.
 */

import type { MachinePort } from "../port/machine-port";
import { h } from "../ui/dom";
import { hex } from "./breakpoints";

export const VIDEO = 0xc480;
const VIDEO_ID = 0x91;
const TYPES = ["text 80×30", "text 40×30", "tile", "bitmap 256", "bitmap 512", "mono 640×480", "type 6", "type 7"];

const base = (unit: number) => `$${hex(0x80000 + unit * 1024, 5)}`;
const bits = (v: number, names: [number, string][]) => names.filter(([b]) => v & b).map(([, n]) => n).join(" · ") || "none";

/** What a layer's registers (8 bytes from +$0) say, in words. */
export function describeLayer(r: Uint8Array, on: boolean): string {
  const config = r[0];
  const type = config & 7;
  const parts = [on ? TYPES[type] : `off (${TYPES[type]})`];
  if (type <= 1) parts.push(`text ${base(r[1])}`, `font ${base(r[2])}`);
  else if (type === 2) parts.push(`map ${base(r[1])}`, `tiles ${base(r[2])}`);
  else parts.push(`image ${base(r[1])}`);
  if (type <= 1 && config & 0x80) parts.push("blink");
  const hs = r[3] | ((r[4] & 3) << 8);
  const vs = r[5] | ((r[6] & 3) << 8);
  if (hs || vs) parts.push(`scroll ${hs},${vs}`);
  if (r[7]) parts.push(`palofs ${r[7]}`);
  return parts.join(" · ");
}

export function devicesView(port: MachinePort) {
  const rows = h("dl", { class: "devs", "data-testid": "dev-video" });
  const status = h("em", {}, "");
  const element = h(
    "div",
    { class: "sec" },
    h("h4", {}, "Video card ", status),
    rows,
    h(
      "p",
      { class: "hint" },
      "Module 9 at $C480. Bases and scroll take effect at vertical blank; this shows what was written.",
    ),
  );

  const row = (name: string, value: string) => [h("dt", {}, name), h("dd", { "data-reg": name }, value)];

  const refresh = async () => {
    const r = await port.readMemory(VIDEO, 0x80, "cpu");
    if (r[0x7f] !== VIDEO_ID) {
      status.textContent = "none";
      rows.replaceChildren(h("dd", {}, "No video card on this machine."));
      return;
    }
    const ctrl = r[0];
    status.textContent = `ID $${hex(r[0x7f], 2)}`;
    const line = r[6] | ((r[7] & 3) << 8);
    rows.replaceChildren(
      ...row("CTRL", `$${hex(ctrl, 2)}  ${bits(ctrl, [[1, "display"], [2, "layer 0"], [4, "layer 1"], [8, "sprites"]])}`),
      ...row("Layer 0", describeLayer(r.subarray(0x10, 0x18), (ctrl & 3) === 3)),
      ...row("Layer 1", describeLayer(r.subarray(0x20, 0x28), (ctrl & 5) === 5)),
      ...row("Palette", `${base(r[2])}  border ${r[1]}`),
      ...row("Sprites", base(r[3])),
      ...row("IEN / ISR", `$${hex(r[4], 2)} / $${hex(r[5], 2)}  ${bits(r[5], [[1, "vblank"], [2, "line"], [4, "collision"]])}`),
      ...row("Beam", `line ${line}${r[8] & 0x80 ? " · vertical blank" : ""}`),
    );
  };
  return { element, refresh };
}
