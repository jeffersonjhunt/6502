// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { describeLayer, devicesView, keyText, USB, VIDEO } from "./devices";

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
});
