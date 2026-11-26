/**
 * The Devices tab: the cards' registers, decoded. So far the video card
 * (hw/design.md, "Video registers") at $C480 and the USB card ("USB card
 * registers") at $C580, read with readMemory, which takes no bus cycle and
 * changes nothing (ISR is read, not cleared; the mouse is not latched).
 * They are read in I/O space, so Apple II mode, where the CPU sees the
 * Apple's $C000-$CFFF, does not hide them. The video card (ID $92,
 * hw/video-rev2.md): its bases shown as the card memory addresses they
 * point at, six digits, base x 256, with the aperture's slots, the data
 * port and the fitted memory; and the card's memory itself (vram.ts: the
 * terminal debugger's `vram`).
 *
 * Then Apple II mode: whether it is on, and the IIe's soft switches
 * (readApple), which keep their state in native mode too. Then the Disk II
 * card (readDisk2): its motor, heads, mode, data register and magnets, and
 * the disks in its drives. Then the sound card at $C500: CTRL, the mailbox,
 * RAMADDR, and both SIDs' registers in words (what was last written: the
 * card answers its write-only registers from that), and its own 6510
 * (readSound): registers, port and CIA 1's timer A. Then the SD card at
 * $C380: the card in the slot (readSdInfo), STATUS, the last error, and the
 * command's UNIT, LBA and COUNT. Then the north bridge's real-time clock
 * (readClock): SEC-YEAR as the bridge last read them from its DS3231, as
 * a date and time, RTCCTL's flags, and the chip's own control, status,
 * alarm, aging and temperature registers; its
 * timebase too: TBCTL, the millisecond counter, MSS and SYNC; and its
 * random source's RNGST and pool.
 */

import type { AppleState, DiskInfo, DiskStatus, Envelope, MachinePort, SdInfo, SoundCpu } from "../port/machine-port";
import { h } from "../ui/dom";
import { cardSize } from "../ui/sd-bar";
import { hex } from "./breakpoints";
import { vramView } from "./vram";

export const VIDEO = 0xc480;
const VIDEO_ID = 0x92;
export const USB = 0xc580;
const USB_ID = 0xb1;
export const SOUND = 0xc500;
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
/** The layer types; 6 and 7 are bitmap 640 and direct 640 (hw/video640.md). */
const TYPES = ["text 80×30", "text 40×30", "tile", "bitmap 256", "bitmap 512", "mono 640×480", "bitmap 640", "direct 640"];

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

/** A base register's two bytes (256-byte card pages), as the card address it names. */
const page = (r: Uint8Array, at: number) => `$${hex((r[at] | (r[at + 1] << 8)) * 256, 6)}`;
const bits = (v: number, names: [number, string][]) => names.filter(([b]) => v & b).map(([, n]) => n).join(" · ") || "none";

const bcd = (v: number) => (v >> 4) * 10 + (v & 15);
const pad = (n: number, w = 2) => String(n).padStart(w, "0");
const DAYS = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
const SQW = ["1 Hz", "1.024 kHz", "4.096 kHz", "8.192 kHz"];

/** SEC-YEAR (the DS3231's $00-$06) as YYYY-MM-DD hh:mm:ss, the day (1 = Sunday) and 12 h mode. */
export function clockTime(t: Uint8Array): string {
  const h12 = (t[2] & 0x40) !== 0;
  const hour = h12 ? (bcd(t[2] & 0x1f) % 12) + (t[2] & 0x20 ? 12 : 0) : bcd(t[2] & 0x3f);
  const year = 2000 + (t[5] & 0x80 ? 100 : 0) + bcd(t[6]);
  const day = DAYS[(t[3] & 7) - 1] ?? `day ${t[3] & 7}`;
  return `${year}-${pad(bcd(t[5] & 0x1f))}-${pad(bcd(t[4] & 0x3f))} ${pad(hour)}:${pad(bcd(t[1] & 0x7f))}:${pad(bcd(t[0] & 0x7f))} · ${day}${h12 ? " · 12 h" : ""}`;
}

/** RTCCTL in words. SET reads 0, so a pending one is BUSY, and `setPending` (the bridge's queue) says so. */
export function rtcctlText(v: number, setPending = false): string {
  return bits(v, [[0x80, "TICK"], [0x20, "ALARM"], [0x04, "LOST"], [0x01, setPending ? "BUSY (SET)" : "BUSY"]]);
}

/** Revision 3's TBCTL in words. */
export function tbctlText(v: number): string {
  return bits(v, [[0x80, "MSF"], [0x40, "OVR"], [0x20, "MSIE"], [0x10, "SYNC"]]);
}

/** Revision 3's RNGST in words. */
export function rngstText(v: number): string {
  return bits(v, [[0x80, "READY"], [0x40, "CHIP"], [0x20, "LOCKED"], [0x10, "FAULT"], [0x01, "BUSY"]]);
}

/** The DS3231's control register ($0E) in words. */
export function clockControl(v: number): string {
  const parts = [v & 0x04 ? "SQW is INT: alarms only" : `square wave ${SQW[(v >> 3) & 3]}`];
  const flags = bits(v, [[0x80, "EOSC"], [0x40, "BBSQW"], [0x20, "CONV"], [0x02, "A2IE"], [0x01, "A1IE"]]);
  if (flags !== "none") parts.push(flags);
  return parts.join(" · ");
}

/** The DS3231's status register ($0F) in words. */
export function clockStatus(v: number): string {
  return bits(v, [[0x80, "OSF (the time is invalid)"], [0x08, "EN32kHz"], [0x04, "BSY"], [0x02, "A2F"], [0x01, "A1F"]]);
}

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

/** The card's frame ($DE03/$DE04): the raster line, the interrupt, and whether $D011/$D012 show the line. */
export function frameText(c: SoundCpu): string {
  return `line ${c.raster} · interrupt ${c.frameEn & 1 ? "on" : "off"}${c.frame & 1 ? " · pending" : ""} · $D012 ${c.frameEn & 2 ? "shows it" : "reads $FF"}`;
}

/** The sound card's ID: revision 3, the card as a device (hw/future/sound-device.md), its only one. */
export const SOUND_V3 = 0xa3;
const TUNE_STATES = ["stopped", "starting", "playing", "paused", "stalled", "jammed", "ended"];
/** Whether a tune's state has the card's 6510 running: starting, playing or stalled (paused holds it). */
export function tuneRuns(state: number): boolean {
  return state === 1 || state === 2 || state === 4;
}
const STREAM_STATES: Record<number, string> = { 0: "idle", 1: "buffering", 2: "playing", 3: "paused", 4: "underrun", 5: "ended", 0x80: "bad format", 0x81: "no sync" };

/** The card's STATUS ($C501) in words. */
export function deviceStatusText(v: number): string {
  return bits(v, [[0x01, "READY"], [0x02, "CMDRDY"], [0x04, "EVT"], [0x08, "XFER"], [0x10, "DATA"], [0x20, "EVLOST"], [0x40, "ERR"], [0x80, "IRQ"]]);
}

/** The card's registers, its firmware's objects and voices, then its 6510 (registers, port, CIA 1's timer, frame) and
 * both SIDs (their shadows). */
export function soundDeviceRows(r: Uint8Array, cpu: SoundCpu | null): HTMLElement[] {
  const row = (name: string, value: string) => [h("dt", {}, name), h("dd", { "data-reg": name }, value)];
  const left = r[0x08] | (r[0x09] << 8) | (r[0x0a] << 16);
  const rows = [
    ...row("STATUS", `$${hex(r[0x01], 2)}  ${deviceStatusText(r[0x01])}${r[0x01] & 0x40 ? ` · ERROR ${r[0x02]}` : ""}`),
    ...row("FIFOs", `CMD ${r[0x04] === 255 ? "255+" : r[0x04]} free · EVT ${r[0x06] === 255 ? "255+" : r[0x06]} waiting`),
    ...row("Transfer", r[0x01] & 0x08 ? `${left} bytes to go · tag $${hex(r[0x0b], 2)}${r[0x00] & 1 ? " · DMA" : ""}` : "none"),
    ...row("IEN / ISR", `$${hex(r[0x0c], 2)} / $${hex(r[0x0d], 2)}  ${bits(r[0x0d], [[1, "EVT"], [2, "XDONE"], [4, "CMDROOM"], [8, "READY"]])}`),
    ...row("Card", `firmware ${r[0x10]}.${r[0x11]} · store ${r[0x0e] / 16} MB · features $${hex(r[0x0f], 2)}`),
  ];
  const d = cpu?.device;
  if (d) {
    rows.push(
      ...row("Tune", TUNE_STATES[d.tune] ?? `state ${d.tune}`),
      ...row("Music", `${["stopped", "playing", "paused"][d.music] ?? d.music} · ${d.notes} notes waiting`),
      ...row("Streams", `0 ${STREAM_STATES[d.stream0] ?? d.stream0} · 1 ${STREAM_STATES[d.stream1] ?? d.stream1}`),
      ...row("Store", `${Math.round(d.storeFree / 1024)} of ${Math.round(d.store / 1024)} KB free · image ${d.image ? "B" : "A"}${d.boot ? " · boot sound" : ""}${d.beep ? " · beep" : ""}`),
      ...row("Voices", `SIDs ${d.voices.slice(0, 6)} · ES5503 ${d.voices.slice(6)}`),
    );
  }
  if (cpu) {
    rows.push(
      ...row("6510", soundCpuText(cpu, d ? tuneRuns(d.tune) : false)),
      ...row("Port", soundPortText(cpu)),
      ...row("Timer A", `$${hex(cpu.timer, 4)} of $${hex(cpu.latch, 4)} · ${cpu.timerOn ? "counting" : "stopped"}`),
      ...row("Frame", frameText(cpu)),
    );
    if (cpu.sids) {
      const sids = cpu.sids;
      const voice = (chip: number, i: number) => {
        const s = sids.subarray(25 * chip + 7 * i, 25 * chip + 7 * i + 7);
        const e = cpu.envelopes[chip * 3 + i];
        return row(`${chip ? "B" : "A"}${i + 1}`, sidVoice(s) + (e ? ` · ${envelopeText(e)}` : ""));
      };
      rows.push(...voice(0, 0), ...voice(0, 1), ...voice(0, 2), ...voice(1, 0), ...voice(1, 1), ...voice(1, 2));
    }
  }
  return rows;
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

/** A layer's registers (hw/video-rev2.md): two-byte bases, the scrolls and PALOFS. */
export function describeLayer(r: Uint8Array, on: boolean): string {
  const config = r[0];
  const type = config & 7;
  const parts = [on ? TYPES[type] : `off (${TYPES[type]})`];
  if (type <= 1) parts.push(`text ${page(r, 1)}`, `font ${page(r, 3)}`);
  else if (type === 2) parts.push(`map ${page(r, 1)}`, `tiles ${page(r, 3)}`);
  else parts.push(`image ${page(r, 1)}`);
  if (type <= 1 && config & 0x80) parts.push("blink");
  const hs = r[5] | ((r[6] & 3) << 8);
  const vs = r[7] | ((r[8] & 3) << 8);
  if (hs || vs) parts.push(`scroll ${hs},${vs}`);
  if (r[9]) parts.push(`palofs ${r[9]}`);
  return parts.join(" · ");
}

export function devicesView(port: MachinePort) {
  const rows = h("dl", { class: "devs", "data-testid": "dev-video" });
  const vram = vramView(port);
  const vramBox = h(
    "div",
    { class: "sec", "data-testid": "dev-vram", hidden: true },
    h("h4", {}, "Card memory"),
    h("p", { class: "hint" }, "The card's own 2 MB, as vram shows it at the terminal's debug> prompt: no bus cycles, the port untouched. ADDR [N] shows N bytes, ADDR = BYTE... writes them."),
    vram.element,
  );
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
  const clockRows = h("dl", { class: "devs", "data-testid": "dev-rtc" });
  const clockStatusEl = h("em", {}, "");
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
    vramBox,
    h("h4", {}, "USB card ", usbStatus),
    usbRows,
    h("p", { class: "hint" }, "Module B at $C580. On the VGA scene the keyboard and mouse are this card's; on Apple II, the keyboard."),
    h("h4", {}, "Apple II mode ", appleStatus),
    appleRows,
    h("p", { class: "hint" }, "The north bridge's MODE and the IIe's soft switches ($C000-$C07F), which keep their state in native mode too."),
    h("h4", {}, "Disk II ", diskStatus),
    diskRows,
    h("p", { class: "hint" }, "Module C; slot 6 in Apple II mode, its switches at $C0E0-$C0EF and its boot ROM at $C600; in native mode the same switches at $C600-$C60F."),
    h("h4", {}, "Sound card ", soundStatus),
    soundRows,
    h("p", { class: "hint" }, "Module A at $C500: two SIDs, and 64 KB for the card's own 6510."),
    h("h4", {}, "SD card ", sdStatus),
    sdRows,
    h("p", { class: "hint" }, "Module 7 at $C380: an SD card behind an RP2350, sectors through DATA by CPU or DMA channel 0. C809R lists it, C80CR boots /BOOT.BIN."),
    h("h4", {}, "Real-time clock ", clockStatusEl),
    clockRows,
    h(
      "p",
      { class: "hint" },
      "The north bridge's DS3231 (ID D): RTCCTL at $C687, SEC-YEAR at $C690-$C696 (reading SEC latches all seven), RTCREG and RTCDATA at $C697-$C698. Set from this computer's clock when the page opened. On revision 3, the timebase: TBCTL at $C6A8, MS0-MS3 at $C6A9-$C6AC, MSS at $C69C-$C69D; and the random source: RNGST, RNGCNT and RNGDATA at $C699-$C69B.",
    ),
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
    if (r[0x7f] !== SOUND_V3) {
      soundStatus.textContent = "none";
      soundRows.replaceChildren(h("dd", {}, "No sound card on this machine."));
      return;
    }
    const cpu = await port.readSound();
    soundRows.replaceChildren(...soundDeviceRows(r, cpu));
    soundStatus.textContent = cpu?.device && tuneRuns(cpu.device.tune) ? "tune playing" : cpu?.device?.tune === 3 ? "tune paused" : "the device";
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

  const refreshClock = async () => {
    const c = await port.readClock();
    if (!c) {
      clockStatusEl.textContent = "none";
      clockRows.replaceChildren(h("dd", {}, "No real-time clock on this machine."));
      return;
    }
    const r = c.chip;
    const alarm = (from: number, n: number) => Array.from(r.subarray(from, from + n), (v) => `$${hex(v, 2)}`).join(" ");
    clockStatusEl.textContent = c.rtcctl & 0x04 ? "lost" : "running";
    clockRows.replaceChildren(
      ...row("Time", clockTime(c.time)),
      ...row("RTCCTL", `$${hex(c.rtcctl, 2)}  ${rtcctlText(c.rtcctl, c.setPending)}`),
      ...row("RTCREG", `$${hex(c.rtcreg, 2)} · RTCDATA $${hex(c.rtcdata, 2)}`),
      ...row("Control", `$${hex(r[0x0e], 2)}  ${clockControl(r[0x0e])}`),
      ...row("Status", `$${hex(r[0x0f], 2)}  ${clockStatus(r[0x0f])}`),
      ...row("Alarms", `1: ${alarm(0x07, 4)} · 2: ${alarm(0x0b, 3)}`),
      ...row("Aging", `$${hex(r[0x10], 2)} · ${r[0x11]}.${pad((r[0x12] >> 6) * 25)} °C`),
      ...(c.timebase
        ? [
            ...row("TBCTL", `$${hex(c.timebase.tbctl, 2)}  ${tbctlText(c.timebase.tbctl)}`),
            ...row("MS", `${c.timebase.ms} · ${(c.timebase.ms / 1000).toFixed(3)} s`),
            ...row("MSS", `${c.timebase.mss}${c.timebase.tbctl & 0x10 ? "" : " · not in step (SYNC clear)"}`),
          ]
        : []),
      ...(c.random ? row("RNGST", `$${hex(c.random.rngst, 2)}  ${rngstText(c.random.rngst)} · ${c.random.count} bytes in the pool`) : []),
    );
  };

  const refresh = async () => {
    await refreshUsb();
    await refreshSd();
    await refreshClock();
    await refreshSound();
    await refreshApple();
    await refreshDisk();
    const r = await port.readMemory(VIDEO, 0x80, "io");
    vramBox.hidden = r[0x7f] !== VIDEO_ID;
    if (!vramBox.hidden) await vram.refresh();
    if (r[0x7f] !== VIDEO_ID) {
      status.textContent = "none";
      rows.replaceChildren(h("dd", {}, "No video card on this machine."));
      return;
    }
    const ctrl = r[0];
    status.textContent = `ID $${hex(r[0x7f], 2)}`;
    const line = r[6] | ((r[7] & 3) << 8);
    const collided = r[8] & 0x0f ? ` · collided: groups $${hex(r[8] & 0x0f, 1)}` : "";
    const slot = (n: number) => `$${hex((r[0x30 + 2 * n] | (r[0x31 + 2 * n] << 8)) * 0x4000, 6)}`;
    const paddr = r[0x38] | (r[0x39] << 8) | (r[0x3a] << 16);
    const pctl = r[0x3e];
    const dir = pctl & 2 ? "from the card" : "to the card";
    rows.replaceChildren(
      ...row("CTRL", `$${hex(ctrl, 2)}  ${bits(ctrl, [[1, "display"], [2, "layer 0"], [4, "layer 1"], [8, "sprites"]])}`),
      ...row("Layer 0", describeLayer(r.subarray(0x10, 0x20), (ctrl & 3) === 3)),
      ...row("Layer 1", describeLayer(r.subarray(0x20, 0x30), (ctrl & 5) === 5)),
      ...row("Palette", `${page(r, 0x0a)}  border ${r[1]}`),
      // SPRCTL ($0E) bit 0: full-resolution sprites (hw/sprites640.md)
      ...row("Sprites", `${ctrl & 8 ? "on" : "off"} · table ${page(r, 0x0c)}${r[0x0e] & 1 ? " · full" : ""}${collided}`),
      ...row("IEN / ISR", `$${hex(r[4], 2)} / $${hex(r[5], 2)}  ${bits(r[5], [[1, "vblank"], [2, "line"], [4, "collision"]])}`),
      ...row("Beam", `line ${line}${r[8] & 0x80 ? " · vertical blank" : ""}`),
      ...row("Splash", `$${hex(r[9], 2)}  ${r[9] & 1 ? "playing (the card's ROM covers the screen)" : "off"}`),
      ...row("Slots", [0, 1, 2, 3].map(slot).join(" ")),
      ...row("Port", `$${hex(paddr, 6)} · step ${r[0x3c] | (r[0x3d] << 8)}${pctl & 4 ? " down" : ""} · ${pctl & 1 ? `DMA ${dir}` : "DMA off"}`),
      ...row("Memory", `${r[0x3f] * 64} KB`),
    );
  };
  return { element, refresh };
}
