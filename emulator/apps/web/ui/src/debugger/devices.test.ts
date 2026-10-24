// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { describeLayer, devicesView, VIDEO } from "./devices";

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
  });

  test("no card: says so", async () => {
    const v = devicesView(new FakePort());
    await v.refresh();
    expect(v.element.textContent).toContain("No video card on this machine.");
    expect(text(v.element, "CTRL")).toBeUndefined();
  });
});
