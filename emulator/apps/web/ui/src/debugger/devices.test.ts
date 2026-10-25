// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import type { AppleState } from "../port/machine-port";
import { appleLanguageCard, appleMemory, appleSlots, appleVideo, describeLayer, devicesView, diskMode, diskText, keyText, trackText, USB, VIDEO } from "./devices";

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
});
