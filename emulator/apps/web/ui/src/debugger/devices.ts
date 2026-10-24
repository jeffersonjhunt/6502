/**
 * The Devices tab: the cards' registers, decoded. So far the video card
 * (hw/design.md, "Video registers") at $C480 and the USB card ("USB card
 * registers") at $C580, read with readMemory, which takes no bus cycle and
 * changes nothing (ISR is read, not cleared; the mouse is not latched).
 * They are read in I/O space, so Apple II mode, where the CPU sees the
 * Apple's $C000-$CFFF, does not hide them. Video bases are shown as the
 * physical addresses they point at: $80000 + base x 1 KB.
 *
 * Then Apple II mode: whether it is on, and the IIe's soft switches
 * (readApple), which keep their state in native mode too.
 */

import type { AppleState, MachinePort } from "../port/machine-port";
import { h } from "../ui/dom";
import { hex } from "./breakpoints";

export const VIDEO = 0xc480;
const VIDEO_ID = 0x91;
export const USB = 0xc580;
const USB_ID = 0xb1;
const MODS = ["L-Ctrl", "L-Shift", "L-Alt", "L-GUI", "R-Ctrl", "R-Shift", "R-Alt", "R-GUI"];

/** KEY as a person reads it: the character, or its control name. */
export function keyText(key: number): string {
  const c = key & 0x7f;
  const names: Record<number, string> = { 0x08: "←/BS", 0x0a: "↓", 0x0b: "↑", 0x0d: "CR", 0x15: "→", 0x1b: "ESC", 0x20: "space", 0x7f: "DEL" };
  const text = names[c] ?? (c < 0x20 ? `^${String.fromCharCode(c + 0x40)}` : String.fromCharCode(c));
  return `$${hex(key, 2)} ${text}${key & 0x80 ? " · new" : ""}`;
}
const TYPES = ["text 80×30", "text 40×30", "tile", "bitmap 256", "bitmap 512", "mono 640×480", "type 6", "type 7"];

/** What the Apple's screen shows, from its switches. */
export function appleVideo(s: AppleState): string {
  const page = s.page2 && !s.store80 ? "page 2" : "page 1";
  const double = s.col80 && s.dhires;
  const text = s.col80 ? "text 80" : "text 40";
  const parts = s.text ? [text] : [`${double ? "double " : ""}${s.hires ? "hi-res" : "lo-res"}${s.mixed ? ` + 4 lines of ${text}` : ""}`];
  parts.push(page);
  if (s.altchar) parts.push("MouseText");
  return parts.join(" · ");
}

/** Which 64 KB the CPU reads and writes, and the language card. */
export function appleMemory(s: AppleState): string {
  const bank = (aux: boolean) => (aux ? "aux" : "main");
  return [`read ${bank(s.ramrd)}`, `write ${bank(s.ramwrt)}`, `zero page ${bank(s.altzp)}`, ...(s.store80 ? ["80STORE"] : [])].join(" · ");
}

export function appleLanguageCard(s: AppleState): string {
  return `$D000 bank ${s.lcBank2 ? 2 : 1} · reads ${s.lcRead ? "RAM" : "ROM"} · ${s.lcWrite ? "writes RAM" : "write-protected"}`;
}

export function appleSlots(s: AppleState): string {
  if (s.intcxrom) return "$C100-$CFFF internal ROM";
  return [`$C300 ${s.slotc3rom ? "slot 3" : "internal 80-column"}`, `$C800 ${s.intc8rom ? "internal" : "slots"}`].join(" · ");
}

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
  const usbRows = h("dl", { class: "devs", "data-testid": "dev-usb" });
  const usbStatus = h("em", {}, "");
  const appleRows = h("dl", { class: "devs", "data-testid": "dev-apple" });
  const appleStatus = h("em", {}, "");
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
    h("h4", {}, "USB card ", usbStatus),
    usbRows,
    h("p", { class: "hint" }, "Module B at $C580. On the VGA scene the keyboard and mouse are this card's; on Apple II, the keyboard."),
    h("h4", {}, "Apple II mode ", appleStatus),
    appleRows,
    h("p", { class: "hint" }, "The north bridge's MODE and the IIe's soft switches ($C000-$C07F), which keep their state in native mode too."),
  );

  const row = (name: string, value: string) => [h("dt", {}, name), h("dd", { "data-reg": name }, value)];

  const refreshUsb = async () => {
    const u = await port.readMemory(USB, 0x80, "io");
    if (u[0x7f] !== USB_ID) {
      usbStatus.textContent = "none";
      usbRows.replaceChildren(h("dd", {}, "No USB card on this machine."));
      return;
    }
    usbStatus.textContent = `ID $${hex(u[0x7f], 2)}`;
    const mods = MODS.filter((_, i) => u[2] & (1 << i)).join(" ") || "none";
    const mx = u[0x12] | (u[0x13] << 8), my = u[0x14] | (u[0x15] << 8);
    usbRows.replaceChildren(
      ...row("KEY", keyText(u[0])),
      ...row("Modifiers", mods),
      ...row("Events", `${u[3]} bytes waiting${u[7] & 0x80 ? " · Pause/Break → /NMI" : ""}`),
      ...row("Mouse", `${u[0x10] & 1 ? "absolute" : "relative"} · buttons $${hex(u[0x11], 2)} · latched ${mx},${my}`),
      ...row("IEN / ISR", `$${hex(u[0x30], 2)} / $${hex(u[0x31], 2)}  ${bits(u[0x31], [[1, "key"], [2, "mouse"], [4, "game"], [8, "device"]])}`),
    );
  };

  const refreshApple = async () => {
    const s = await port.readApple();
    if (!s) {
      appleStatus.textContent = "none";
      appleRows.replaceChildren(h("dd", {}, "No Apple II mode on this machine."));
      return;
    }
    appleStatus.textContent = s.mode ? "on" : "off";
    appleRows.replaceChildren(
      ...row("Mode", s.mode ? "on · the IIe's map · 1.02 MHz" : "off (C806R, or Start Apple IIe)"),
      ...row("Video", appleVideo(s)),
      ...row("Memory", appleMemory(s)),
      ...row("Language card", appleLanguageCard(s)),
      ...row("Slots", appleSlots(s)),
      ...row("Annunciators", `AN0-AN3 ${[0, 1, 2, 3].map((n) => (s.an >> n) & 1).join("")}${s.ioudis ? " · IOUDIS" : ""}`),
    );
  };

  const refresh = async () => {
    await refreshUsb();
    await refreshApple();
    const r = await port.readMemory(VIDEO, 0x80, "io");
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
      ...row("Sprites", `${ctrl & 8 ? "on" : "off"} · table ${base(r[3])}${r[8] & 0x0f ? ` · collided: groups $${hex(r[8] & 0x0f, 1)}` : ""}`),
      ...row("IEN / ISR", `$${hex(r[4], 2)} / $${hex(r[5], 2)}  ${bits(r[5], [[1, "vblank"], [2, "line"], [4, "collision"]])}`),
      ...row("Beam", `line ${line}${r[8] & 0x80 ? " · vertical blank" : ""}`),
    );
  };
  return { element, refresh };
}
