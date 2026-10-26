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
 * (readApple), which keep their state in native mode too. Then the Disk II
 * card (readDisk2): its motor, heads, mode, data register and magnets, and
 * the disks in its drives. Then the sound card at $C500: CTRL, the mailbox,
 * RAMADDR, and both SIDs' registers in words (what was last written: the
 * card answers its write-only registers from that), and its own 6510
 * (readSound): registers, port and CIA 1's timer A. Then the SD card at
 * $C380: the card in the slot (readSdInfo), STATUS, the last error, and the
 * command's UNIT, LBA and COUNT.
 */

import type { AppleState, DiskInfo, DiskStatus, Envelope, MachinePort, SdInfo, SoundCpu } from "../port/machine-port";
import { h } from "../ui/dom";
import { cardSize } from "../ui/sd-bar";
import { hex } from "./breakpoints";

export const VIDEO = 0xc480;
const VIDEO_ID = 0x91;
export const USB = 0xc580;
const USB_ID = 0xb1;
export const SOUND = 0xc500;
const SOUND_ID = 0xa1;
export const SD = 0xc380;
const SD_ID = 0x71;
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

/** A head's place: track and quarters, as "17", "17.25", "17.5". */
export function trackText(quarter: number): string {
  return String(quarter / 4);
}

export function diskMode(s: DiskStatus): string {
  if (s.q7) return s.q6 ? "write: load the data bus" : "write: shift out";
  return s.q6 ? "sense write-protect" : "read";
}

export function diskText(d: DiskInfo | null): string {
  if (!d) return "empty";
  const how = { woz: "WOZ", dsk: "16 sectors, DOS order", po: "16 sectors, ProDOS order" }[d.format];
  return [d.name || "(no name)", how, ...(d.writeProtected ? ["write-protected"] : []), ...(d.changed ? ["written to"] : [])].join(" · ");
}

/** The SD card's STATUS in words. */
export function sdStatusText(v: number): string {
  if (!(v & 0x20)) return "no card";
  const parts = [v & 0x80 ? "busy" : v & 0x40 ? "ready" : "starting up"];
  if (v & 0x08) parts.push("DATA");
  if (v & 0x10) parts.push("write-protected");
  if (v & 0x01) parts.push("error");
  return parts.join(" · ");
}

/** The SD card's ERROR codes (hw/design.md). */
export function sdErrorText(code: number): string {
  const names = ["none", "no card", "out of range", "CRC", "timeout", "write-protected", "unit not mounted", "unknown command", "not found", "no FAT volume"];
  return names[code] ?? `$${hex(code, 2)}`;
}

export function sdCardText(c: SdInfo | null): string {
  if (!c) return "empty";
  return [c.name || "(no name)", `${c.sectors} sectors (${cardSize(c.sectors)})`, ...(c.writeProtected ? ["write-protected"] : []), ...(c.changed ? ["written to"] : [])].join(" · ");
}

const base = (unit: number) => `$${hex(0x80000 + unit * 1024, 5)}`;
const bits = (v: number, names: [number, string][]) => names.filter(([b]) => v & b).map(([, n]) => n).join(" · ") || "none";

/** What a layer's registers (8 bytes from +$0) say, in words. */
/** A SID voice's seven registers in words. */
export function sidVoice(r: Uint8Array): string {
  const ctrl = r[4];
  const waves = bits(ctrl, [[0x10, "triangle"], [0x20, "saw"], [0x40, "pulse"], [0x80, "noise"]]);
  const parts = [`$${hex(r[0] | (r[1] << 8), 4)}`, waves === "none" ? "no waveform" : waves];
  if (ctrl & 0x40) parts.push(`width $${hex(r[2] | ((r[3] & 0x0f) << 8), 3)}`);
  const flags = bits(ctrl, [[0x01, "gate"], [0x02, "sync"], [0x04, "ring"], [0x08, "test"]]);
  if (flags !== "none") parts.push(flags);
  parts.push(`ADSR ${[r[5] >> 4, r[5] & 15, r[6] >> 4, r[6] & 15].map((n) => hex(n, 1)).join(" ")}`);
  return parts.join(" · ");
}

/** A voice's envelope in words. */
export function envelopeText(e: Envelope): string {
  return `env $${hex(e.level, 2)} ${e.held ? "held at 0" : e.phase}`;
}

/** A SID's volume and filter ($15-$18), and what it reads back ($1B-$1C), in words. */
export function sidFilter(r: Uint8Array): string {
  const mode = r[0x18];
  const types = bits(mode, [[0x10, "low"], [0x20, "band"], [0x40, "high"]]);
  const routed = ["1", "2", "3", "ext"].filter((_, i) => r[0x17] & (1 << i));
  const parts = [`volume ${mode & 15}`];
  if (types === "none" || !routed.length) parts.push("filter off");
  else parts.push(`filter ${types.replace(/ · /g, "+")}-pass · cutoff ${(r[0x15] & 7) | (r[0x16] << 3)} · resonance ${r[0x17] >> 4} · voices ${routed.join(" ")}`);
  if (mode & 0x80) parts.push("voice 3 off");
  parts.push(`OSC3 $${hex(r[0x1b], 2)} ENV3 $${hex(r[0x1c], 2)}`);
  return parts.join(" · ");
}

/** The card's 6510 in a line: its registers, and whether it runs. */
export function soundCpuText(c: SoundCpu, running: boolean): string {
  const regs = `PC $${hex(c.pc, 4)} · A $${hex(c.a, 2)} X $${hex(c.x, 2)} Y $${hex(c.y, 2)} S $${hex(c.s, 2)} P $${hex(c.p, 2)}`;
  return `${regs} · ${c.jammed ? "jammed (CTRL RESET)" : running ? "running" : "held"}`;
}

/** The 6510's port, and what it banks at $D000-$DFFF. */
export function soundPortText(c: SoundCpu): string {
  const lines = (c.port | (~c.ddr & 0xff)) & 7;
  return `DDR $${hex(c.ddr, 2)} · $${hex(c.port, 2)} · ${lines & 4 && lines & 3 ? "I/O in" : "RAM at $D000"}`;
}

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
  const diskRows = h("dl", { class: "devs", "data-testid": "dev-disk2" });
  const diskStatus = h("em", {}, "");
  const soundRows = h("dl", { class: "devs", "data-testid": "dev-sound" });
  const soundStatus = h("em", {}, "");
  const sdRows = h("dl", { class: "devs", "data-testid": "dev-sd" });
  const sdStatus = h("em", {}, "");
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
    h("h4", {}, "Disk II ", diskStatus),
    diskRows,
    h("p", { class: "hint" }, "Module C; slot 6 in Apple II mode, its switches at $C0E0-$C0EF and its boot ROM at $C600."),
    h("h4", {}, "Sound card ", soundStatus),
    soundRows,
    h("p", { class: "hint" }, "Module A at $C500: two SIDs, and 64 KB for the card's own 6510."),
    h("h4", {}, "SD card ", sdStatus),
    sdRows,
    h("p", { class: "hint" }, "Module 7 at $C380: an SD card behind an RP2350, sectors through DATA by CPU or DMA channel 0. C809R lists it, C80CR boots /BOOT.BIN."),
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

  const refreshDisk = async () => {
    const s = await port.readDisk2();
    if (!s) {
      diskStatus.textContent = "none";
      diskRows.replaceChildren(h("dd", {}, "No Disk II card on this machine."));
      return;
    }
    diskStatus.textContent = s.spinning ? "spinning" : "stopped";
    const motor = s.motor ? "motor on" : s.spinning ? "running on" : "motor off";
    const [d1, d2] = await Promise.all([port.readDiskInfo(0), port.readDiskInfo(1)]);
    const magnets = [0, 1, 2, 3].filter((m) => s.phases & (1 << m)).join(" ") || "none";
    diskRows.replaceChildren(
      ...row("Drive", `${s.drive + 1} · ${motor}`),
      ...row("Heads", `drive 1 track ${trackText(s.quarterTracks[0])} · drive 2 track ${trackText(s.quarterTracks[1])}`),
      ...row("Mode", diskMode(s)),
      ...row("Data", `$${hex(s.data, 2)}`),
      ...row("Phases", magnets),
      ...row("Drive 1", diskText(d1)),
      ...row("Drive 2", diskText(d2)),
    );
  };

  const refreshSound = async () => {
    const r = await port.readMemory(SOUND, 0x80, "io");
    if (r[0x7f] !== SOUND_ID) {
      soundStatus.textContent = "none";
      soundRows.replaceChildren(h("dd", {}, "No sound card on this machine."));
      return;
    }
    const ctrl = r[0x40], status = r[0x41];
    soundStatus.textContent = ctrl & 2 ? "running" : "stopped";
    const clock = ctrl & 1 ? "NTSC 1.023 MHz" : "PAL 0.985 MHz";
    const box = (full: boolean, v: number) => (full ? `$${hex(v, 2)} waiting` : "empty");
    const cpu = await port.readSound();
    const envelope = (chip: number, i: number) => (cpu?.envelopes[chip * 3 + i] ? ` · ${envelopeText(cpu.envelopes[chip * 3 + i])}` : "");
    const voices = (base: number) =>
      [0, 7, 14].map((o, i) => row(`${base ? "B" : "A"}${i + 1}`, sidVoice(r.subarray(base + o, base + o + 7)) + envelope(base ? 1 : 0, i))).flat();
    const cpuRows = cpu
      ? [
          ...row("6510", soundCpuText(cpu, !!(ctrl & 2))),
          ...row("Port", soundPortText(cpu)),
          ...row("Timer A", `$${hex(cpu.timer, 4)} of $${hex(cpu.latch, 4)} · ${cpu.timerOn ? "counting" : "stopped"}`),
        ]
      : [];
    soundRows.replaceChildren(
      ...row("CTRL", `$${hex(ctrl, 2)}  ${clock} · 6510 ${ctrl & 2 ? "running" : "stopped"} · SID B at $${ctrl & 8 ? "D500" : "D420"}${ctrl & 0x10 ? " · A to both" : ""}`),
      ...row("Mailbox", `to card ${status & 2 ? "full" : "empty"} · to host ${box(!!(status & 4), r[0x47])}${r[0x42] & 4 ? " · interrupts" : ""}`),
      ...row("RAMADDR", `$${hex(r[0x43] | (r[0x44] << 8), 4)}`),
      ...cpuRows,
      ...row("SID A", sidFilter(r.subarray(0x00, 0x1d))),
      ...voices(0x00),
      ...row("SID B", sidFilter(r.subarray(0x20, 0x3d))),
      ...voices(0x20),
    );
  };

  const refreshSd = async () => {
    const r = await port.readMemory(SD, 0x80, "io");
    if (r[0x7f] !== SD_ID) {
      sdStatus.textContent = "none";
      sdRows.replaceChildren(h("dd", {}, "No SD card slot on this machine."));
      return;
    }
    const st = r[0x01];
    sdStatus.textContent = st & 0x20 ? (st & 0x80 ? "busy" : "card in") : "empty";
    const lba = (r[0x06] | (r[0x07] << 8) | (r[0x08] << 16) | (r[0x09] << 24)) >>> 0;
    const count = r[0x0a] | (r[0x0b] << 8);
    sdRows.replaceChildren(
      ...row("Card", sdCardText(await port.readSdInfo())),
      ...row("STATUS", `$${hex(st, 2)}  ${sdStatusText(st)}`),
      ...row("ERROR", st & 0x01 ? sdErrorText(r[0x02]) : "none"),
      ...row("Transfer", `unit ${r[0x04]} · LBA $${hex(lba, 8)} · COUNT ${count || 65536}${r[0x00] & 1 ? " · DMA" : ""}`),
      ...row("IEN / ISR", `$${hex(r[0x10], 2)} / $${hex(r[0x11], 2)}  ${bits(r[0x11], [[1, "done"], [2, "card change"]])}`),
    );
  };

  const refresh = async () => {
    await refreshUsb();
    await refreshSd();
    await refreshSound();
    await refreshApple();
    await refreshDisk();
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
