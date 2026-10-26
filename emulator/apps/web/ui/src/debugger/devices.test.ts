// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import type { AppleState, Envelope, SoundCpu } from "../port/machine-port";
import { appleLanguageCard, clockControl, clockStatus, clockTime, rtcctlText, appleMemory, appleSlots, appleVideo, describeLayer, devicesView, diskMode, diskText, keyText, SD, sdCardText, sdErrorText, sdStatusText, sidFilter, sidVoice, SOUND, soundCpuText, soundPortText, trackText, USB, VIDEO } from "./devices";

/** The IIe's switches at power-on. */
const IIE: AppleState = {
  mode: false, text: true, mixed: false, page2: false, hires: false, col80: false, store80: false, altchar: false, dhires: false,
  ramrd: false, ramwrt: false, altzp: false, intcxrom: false, slotc3rom: false, intc8rom: false,
  lcBank2: true, lcRead: false, lcWrite: true, ioudis: true, an: 0,
};

const text = (el: Element, reg: string) => el.querySelector(`[data-reg="${reg}"]`)?.textContent;

describe("Devices tab", () => {
  test("a layer's registers in words", () => {
    expect(describeLayer(Uint8Array.from([0x80, 8, 1, 0, 0, 0, 0, 0]), true)).toBe(
      "text 80×30 · text $82000 · font $80400 · blink",
    );
    expect(describeLayer(Uint8Array.from([0x05, 64, 0, 0x2c, 0x01, 3, 0, 2]), false)).toBe(
      "off (mono 640×480) · image $90000 · scroll 300,3 · palofs 2",
    );
    expect(describeLayer(Uint8Array.from([0x02, 4, 16, 0, 0, 0, 0, 0]), true)).toBe("tile · map $81000 · tiles $84000");
  });

  test("the video card as the BIOS leaves it", async () => {
    const port = new FakePort();
    const regs: Record<number, number> = { 0x00: 0x03, 0x04: 0x00, 0x05: 0x01, 0x06: 0xe0, 0x07: 0x01, 0x08: 0x80, 0x10: 0x80, 0x11: 8, 0x12: 1, 0x7f: 0x91 };
    for (const [o, v] of Object.entries(regs)) port.memory.set(VIDEO + Number(o), v);
    const v = devicesView(port);
    await v.refresh();
    expect(v.element.querySelector("h4")!.textContent).toContain("ID $91");
    expect(text(v.element, "CTRL")).toBe("$03  display · layer 0");
    expect(text(v.element, "Layer 0")).toBe("text 80×30 · text $82000 · font $80400 · blink");
    expect(text(v.element, "Layer 1")).toBe("off (text 80×30) · text $80000 · font $80000");
    expect(text(v.element, "IEN / ISR")).toBe("$00 / $01  vblank");
    expect(text(v.element, "Beam")).toBe("line 480 · vertical blank");
    expect(text(v.element, "Sprites")).toBe("off · table $80000");
    port.memory.set(VIDEO + 0x00, 0x0b);
    port.memory.set(VIDEO + 0x03, 40);
    port.memory.set(VIDEO + 0x08, 0x86); // in vertical blank, groups 2 and 4 collided
    await v.refresh();
    expect(text(v.element, "Sprites")).toBe("on · table $8A000 · collided: groups $6");
  });

  test("no card: says so", async () => {
    const v = devicesView(new FakePort());
    await v.refresh();
    expect(v.element.textContent).toContain("No video card on this machine.");
    expect(text(v.element, "CTRL")).toBeUndefined();
  });

  test("the USB card: KEY, modifiers, events, the mouse, interrupts", async () => {
    const port = new FakePort();
    const regs: Record<number, number> = { 0x00: 0xc1, 0x02: 0x22, 0x03: 4, 0x07: 0x80, 0x10: 1, 0x11: 0x01, 0x12: 0x40, 0x13: 0x01, 0x14: 0xf0, 0x31: 0x03, 0x7f: 0xb1 };
    for (const [o, v] of Object.entries(regs)) port.memory.set(USB + Number(o), v);
    const v = devicesView(port);
    await v.refresh();
    expect(text(v.element, "KEY")).toBe("$C1 A · new");
    expect(text(v.element, "Modifiers")).toBe("L-Shift R-Shift");
    expect(text(v.element, "Events")).toBe("4 bytes waiting · Pause/Break → /NMI");
    expect(text(v.element, "Mouse")).toBe("absolute · buttons $01 · latched 320,240");
    expect(text(v.element, "IEN / ISR")).toBe("$00 / $03  key · mouse");
    expect(keyText(0x0d)).toBe("$0D CR");
    expect(keyText(0x83)).toBe("$83 ^C · new");
    expect(keyText(0x15)).toBe("$15 →");
  });

  test("no USB card: says so", async () => {
    const v = devicesView(new FakePort());
    await v.refresh();
    expect(v.element.textContent).toContain("No USB card on this machine.");
  });

  test("in Apple II mode the cards are read in I/O space, where the CPU sees the Apple's", async () => {
    const port = new FakePort();
    port.memory.set(VIDEO + 0x7f, 0x60); // what the CPU reads at $C4FF now: the Apple's slot 4
    port.io.set(VIDEO + 0x7f, 0x91);
    port.io.set(USB + 0x7f, 0xb1);
    const v = devicesView(port);
    await v.refresh();
    expect(v.element.textContent).toContain("Video card ID $91");
    expect(v.element.textContent).toContain("USB card ID $B1");
  });

  test("the Apple's screen, memory, language card and slots in words", () => {
    expect(appleVideo(IIE)).toBe("text 40 · page 1");
    expect(appleVideo({ ...IIE, text: false, mixed: true, page2: true })).toBe("lo-res + 4 lines of text 40 · page 2");
    expect(appleVideo({ ...IIE, text: false, hires: true, col80: true, dhires: true, page2: true, store80: true })).toBe("double hi-res · page 1");
    expect(appleVideo({ ...IIE, text: false, hires: true, col80: true })).toBe("hi-res · page 1"); // AN3 on: not double
    expect(appleVideo({ ...IIE, text: false, hires: true, dhires: true })).toBe("hi-res · page 1"); // nor without 80COL
    expect(appleVideo({ ...IIE, col80: true, altchar: true })).toBe("text 80 · page 1 · MouseText");
    expect(appleMemory(IIE)).toBe("read main · write main · zero page main");
    expect(appleMemory({ ...IIE, ramrd: true, altzp: true, store80: true })).toBe("read aux · write main · zero page aux · 80STORE");
    expect(appleLanguageCard(IIE)).toBe("$D000 bank 2 · reads ROM · writes RAM");
    expect(appleLanguageCard({ ...IIE, lcBank2: false, lcRead: true, lcWrite: false })).toBe("$D000 bank 1 · reads RAM · write-protected");
    expect(appleSlots(IIE)).toBe("$C300 internal 80-column · $C800 slots");
    expect(appleSlots({ ...IIE, slotc3rom: true, intc8rom: true })).toBe("$C300 slot 3 · $C800 internal");
    expect(appleSlots({ ...IIE, intcxrom: true })).toBe("$C100-$CFFF internal ROM");
  });

  test("the Apple II section: off, on, or none", async () => {
    const port = new FakePort();
    const v = devicesView(port);
    await v.refresh();
    expect(v.element.textContent).toContain("No Apple II mode on this machine.");
    port.apple = IIE;
    await v.refresh();
    expect(text(v.element, "Mode")).toBe("off (C806R, or Start Apple IIe)");
    port.apple = { ...IIE, mode: true, an: 0b1010, ioudis: false };
    await v.refresh();
    expect(text(v.element, "Mode")).toBe("on · the IIe's map · 1.02 MHz");
    expect(text(v.element, "Video")).toBe("text 40 · page 1");
    expect(text(v.element, "Annunciators")).toBe("AN0-AN3 0101");
  });

  test("the Disk II: motor, heads, mode, data, magnets, disks", async () => {
    const port = new FakePort();
    const v = devicesView(port);
    await v.refresh();
    expect(v.element.textContent).toContain("No Disk II card on this machine.");
    port.disk2 = { motor: false, spinning: true, drive: 0, q6: false, q7: false, data: 0xd5, phases: 0b0101, quarterTracks: [69, 0] };
    port.disks[0] = { name: "Master.dsk", format: "dsk", writeProtected: true, changed: true, writes: 3 };
    await v.refresh();
    expect(text(v.element, "Drive")).toBe("1 · running on");
    expect(text(v.element, "Heads")).toBe("drive 1 track 17.25 · drive 2 track 0");
    expect(text(v.element, "Mode")).toBe("read");
    expect(text(v.element, "Data")).toBe("$D5");
    expect(text(v.element, "Phases")).toBe("0 2");
    expect(text(v.element, "Drive 1")).toBe("Master.dsk · 16 sectors, DOS order · write-protected · written to");
    expect(text(v.element, "Drive 2")).toBe("empty");
    expect(trackText(70)).toBe("17.5");
    expect(diskMode({ ...port.disk2, q7: true, q6: true })).toBe("write: load the data bus");
    expect(diskMode({ ...port.disk2, q7: true })).toBe("write: shift out");
    expect(diskMode({ ...port.disk2, q6: true })).toBe("sense write-protect");
    expect(diskText({ name: "t.woz", format: "woz", writeProtected: false, changed: false, writes: 0 })).toBe("t.woz · WOZ");
  });

  test("a SID voice and its filter in words", () => {
    expect(sidVoice(Uint8Array.from([0xd6, 0x1c, 0x00, 0x08, 0x41, 0x09, 0xa0]))).toBe("$1CD6 · pulse · width $800 · gate · ADSR 0 9 A 0");
    expect(sidVoice(Uint8Array.from([0x00, 0x10, 0, 0, 0x16, 0x00, 0x00]))).toBe("$1000 · triangle · sync · ring · ADSR 0 0 0 0");
    expect(sidVoice(new Uint8Array(7))).toBe("$0000 · no waveform · ADSR 0 0 0 0");
    const r = new Uint8Array(29);
    r[0x18] = 0x0f;
    expect(sidFilter(r)).toBe("volume 15 · filter off · OSC3 $00 ENV3 $00");
    r[0x15] = 0x07;
    r[0x16] = 0x80;
    r[0x17] = 0xf5; // resonance 15, voices 1 and 3
    r[0x18] = 0xda; // low and high pass, voice 3 off, volume 10
    expect(sidFilter(r)).toBe("volume 10 · filter low+high-pass · cutoff 1031 · resonance 15 · voices 1 3 · voice 3 off · OSC3 $00 ENV3 $00");
    r[0x17] = 0x08;
    r[0x1b] = 0xa5;
    r[0x1c] = 0x3c;
    expect(sidFilter(r)).toBe("volume 10 · filter low+high-pass · cutoff 1031 · resonance 0 · voices ext · voice 3 off · OSC3 $A5 ENV3 $3C");
  });

  test("the sound card: CTRL, the mailbox, RAMADDR and both SIDs", async () => {
    const port = new FakePort();
    const regs: Record<number, number> = { 0x00: 0xd6, 0x01: 0x1c, 0x04: 0x21, 0x18: 0x0f, 0x24: 0x11, 0x40: 0x1b, 0x41: 0x06, 0x42: 0x04, 0x43: 0x00, 0x44: 0x10, 0x47: 0x80, 0x7f: 0xa1 };
    for (const [o, v] of Object.entries(regs)) port.memory.set(SOUND + Number(o), v);
    const v = devicesView(port);
    await v.refresh();
    expect(v.element.textContent).toContain("Sound card running");
    expect(text(v.element, "CTRL")).toBe("$1B  NTSC 1.023 MHz · 6510 running · SID B at $D500 · A to both");
    expect(text(v.element, "Mailbox")).toBe("to card full · to host $80 waiting · interrupts");
    expect(text(v.element, "RAMADDR")).toBe("$1000");
    expect(text(v.element, "SID A")).toBe("volume 15 · filter off · OSC3 $00 ENV3 $00");
    expect(text(v.element, "A1")).toBe("$1CD6 · saw · gate · ADSR 0 0 0 0");
    expect(text(v.element, "B1")).toBe("$0000 · triangle · gate · ADSR 0 0 0 0");
    expect(text(v.element, "B3")).toBe("$0000 · no waveform · ADSR 0 0 0 0");
  });

  test("the card's 6510: registers, port, timer", async () => {
    const port = new FakePort();
    port.memory.set(SOUND + 0x7f, 0xa1);
    port.memory.set(SOUND + 0x40, 0x02);
    const held: Envelope = { level: 0, phase: "release", held: true };
    const cpu: SoundCpu = {
      pc: 0x1023, a: 1, x: 2, y: 3, s: 0xfd, p: 0x24, jammed: false, cycles: 5000, ddr: 0x2f, port: 0x37, timer: 0x1234, latch: 0x4cf8, timerOn: true,
      envelopes: [{ level: 0x88, phase: "decay/sustain", held: false }, held, held, held, held, { level: 0x40, phase: "attack", held: false }],
    };
    port.sound = cpu;
    const v = devicesView(port);
    await v.refresh();
    expect(text(v.element, "6510")).toBe("PC $1023 · A $01 X $02 Y $03 S $FD P $24 · running");
    expect(text(v.element, "Port")).toBe("DDR $2F · $37 · I/O in");
    expect(text(v.element, "Timer A")).toBe("$1234 of $4CF8 · counting");
    expect(text(v.element, "A1")).toBe("$0000 · no waveform · ADSR 0 0 0 0 · env $88 decay/sustain");
    expect(text(v.element, "A2")).toBe("$0000 · no waveform · ADSR 0 0 0 0 · env $00 held at 0");
    expect(text(v.element, "B3")).toBe("$0000 · no waveform · ADSR 0 0 0 0 · env $40 attack");
    expect(soundCpuText({ ...cpu, jammed: true }, true)).toBe("PC $1023 · A $01 X $02 Y $03 S $FD P $24 · jammed (CTRL RESET)");
    expect(soundCpuText(cpu, false)).toContain("· held");
    expect(soundPortText({ ...cpu, port: 0x34 })).toBe("DDR $2F · $34 · RAM at $D000");
    expect(soundPortText({ ...cpu, ddr: 0, port: 0 })).toBe("DDR $00 · $00 · I/O in"); // inputs read high
  });

  test("no sound card: says so", async () => {
    const v = devicesView(new FakePort());
    await v.refresh();
    expect(v.element.textContent).toContain("No sound card on this machine.");
  });

  test("the SD card's registers and card in words", async () => {
    expect(sdStatusText(0x00)).toBe("no card");
    expect(sdStatusText(0x20)).toBe("starting up");
    expect(sdStatusText(0x60)).toBe("ready");
    expect(sdStatusText(0xe8)).toBe("busy · DATA");
    expect(sdStatusText(0x71)).toBe("ready · write-protected · error");
    expect(sdErrorText(9)).toBe("no FAT volume");
    expect(sdErrorText(0x42)).toBe("$42");
    expect(sdCardText(null)).toBe("empty");
    expect(sdCardText({ name: "card.img", sectors: 65536, writeProtected: true, changed: true })).toBe("card.img · 65536 sectors (32 MB) · write-protected · written to");
    expect(sdCardText({ name: "", sectors: 2880, writeProtected: false, changed: false })).toBe("(no name) · 2880 sectors (1440 KB)");

    const port = new FakePort();
    const view = devicesView(port);
    await view.refresh();
    expect(view.element.querySelector('[data-testid="dev-sd"]')?.textContent).toBe("No SD card slot on this machine.");
    const regs: Record<number, number> = { 0x00: 0x01, 0x01: 0x61, 0x02: 0x08, 0x04: 1, 0x06: 0x34, 0x07: 0x12, 0x0a: 0, 0x0b: 0, 0x10: 1, 0x11: 3, 0x7f: 0x71 };
    for (const [o, v] of Object.entries(regs)) port.io.set(SD + Number(o), v);
    await port.insertSd(new Uint8Array(2880 * 512), "fat12.img");
    await view.refresh();
    const sd = view.element.querySelector('[data-testid="dev-sd"]')!;
    expect(text(sd, "Card")).toBe("fat12.img · 2880 sectors (1440 KB)");
    expect(text(sd, "STATUS")).toBe("$61  ready · error");
    expect(text(sd, "ERROR")).toBe("not found");
    expect(text(sd, "Transfer")).toBe("unit 1 · LBA $00001234 · COUNT 65536 · DMA");
    expect(text(sd, "IEN / ISR")).toBe("$01 / $03  done · card change");
  });

  test("the real-time clock: the time, RTCCTL and the DS3231's registers in words", async () => {
    const bytes = (...v: number[]) => Uint8Array.from(v);
    expect(clockTime(bytes(0x07, 0x45, 0x13, 0x02, 0x26, 0x10, 0x26))).toBe("2026-10-26 13:45:07 · Monday");
    expect(clockTime(bytes(0x00, 0x00, 0x52, 0x01, 0x01, 0x81, 0x00))).toBe("2100-01-01 00:00:00 · Sunday · 12 h"); // 12 AM
    expect(clockTime(bytes(0x59, 0x30, 0x71, 0x07, 0x29, 0x02, 0x00))).toBe("2000-02-29 23:30:59 · Saturday · 12 h"); // 11 PM
    expect(clockTime(bytes(0, 0, 0, 0, 1, 1, 0))).toBe("2000-01-01 00:00:00 · day 0");
    expect(rtcctlText(0x00)).toBe("none");
    expect(rtcctlText(0xc5)).toBe("TICK · TIE · LOST · BUSY");
    expect(clockControl(0x00)).toBe("square wave 1 Hz");
    expect(clockControl(0x1c)).toBe("SQW is INT: alarms only");
    expect(clockControl(0x89)).toBe("square wave 1.024 kHz · EOSC · A1IE");
    expect(clockStatus(0x88)).toBe("OSF (the time is invalid) · EN32kHz");
    expect(clockStatus(0x07)).toBe("BSY · A2F · A1F");

    const port = new FakePort();
    const view = devicesView(port);
    await view.refresh();
    expect(view.element.querySelector('[data-testid="dev-rtc"]')?.textContent).toBe("No real-time clock on this machine.");
    const chip = new Uint8Array(0x13);
    chip.set([0x80, 0x80, 0x80, 0x80], 0x07);
    chip[0x0f] = 0x09;
    chip[0x10] = 0x85;
    chip[0x11] = 25;
    port.clock = { time: bytes(0x07, 0x45, 0x13, 0x02, 0x26, 0x10, 0x26), rtcctl: 0xe0, rtcreg: 0x0f, rtcdata: 0x09, chip };
    await view.refresh();
    const rtc = view.element.querySelector('[data-testid="dev-rtc"]')!;
    expect(rtc.previousElementSibling?.textContent).toBe("Real-time clock ticking");
    expect(text(rtc, "Time")).toBe("2026-10-26 13:45:07 · Monday");
    expect(text(rtc, "RTCCTL")).toBe("$E0  TICK · TIE · ALARM");
    expect(text(rtc, "RTCREG")).toBe("$0F · RTCDATA $09");
    expect(text(rtc, "Control")).toBe("$00  square wave 1 Hz");
    expect(text(rtc, "Status")).toBe("$09  EN32kHz · A1F");
    expect(text(rtc, "Alarms")).toBe("1: $80 $80 $80 $80 · 2: $00 $00 $00");
    expect(text(rtc, "Aging")).toBe("$85 · 25.00 °C");
    port.clock = { ...port.clock, rtcctl: 0x04 };
    await view.refresh();
    expect(rtc.previousElementSibling?.textContent).toBe("Real-time clock lost");
  });
});
