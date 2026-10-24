import { describe, expect, test } from "vitest";
import { UsbKeyboard, usageFor } from "./usb-keyboard";

const ev = (code: string, more: Partial<KeyboardEvent> = {}) => ({ code, metaKey: false, ...more }) as KeyboardEvent;

describe("USB keyboard", () => {
  test("physical keys to HID usages", () => {
    expect(usageFor("KeyA")).toBe(0x04);
    expect(usageFor("KeyZ")).toBe(0x1d);
    expect(usageFor("Digit1")).toBe(0x1e);
    expect(usageFor("Digit0")).toBe(0x27);
    expect(usageFor("Numpad1")).toBe(0x59);
    expect(usageFor("Numpad0")).toBe(0x62);
    expect(usageFor("F1")).toBe(0x3a);
    expect(usageFor("F12")).toBe(0x45);
    expect(usageFor("F13")).toBeNull();
    expect(usageFor("Enter")).toBe(0x28);
    expect(usageFor("ArrowUp")).toBe(0x52);
    expect(usageFor("Pause")).toBe(0x48);
    expect(usageFor("ShiftRight")).toBe(0xe5);
    expect(usageFor("MetaLeft")).toBe(0xe3);
    expect(usageFor("Fn")).toBeNull();
  });

  test("down and up once each; host repeats swallowed; Cmd shortcuts left alone; focus loss releases", () => {
    const sent: [number, boolean][] = [];
    const k = new UsbKeyboard((u, d) => sent.push([u, d]));
    const shift = { shiftKey: true };            // as a browser flags every event while Shift is held
    expect(k.down(ev("ShiftLeft", shift))).toBe(true);
    expect(k.down(ev("KeyA", shift))).toBe(true);
    expect(k.down(ev("KeyA", shift))).toBe(true); // the host's auto-repeat: taken, not sent
    expect(k.up(ev("KeyA", shift))).toBe(true);
    expect(k.up(ev("KeyA", shift))).toBe(false); // not held
    expect(k.down(ev("KeyR", { metaKey: true, shiftKey: true }))).toBe(false); // Cmd-R: the browser's
    expect(k.down(ev("Fn", shift))).toBe(false);
    k.down(ev("KeyB", shift));
    k.releaseAll();
    expect(sent).toEqual([
      [0xe1, true],
      [0x04, true],
      [0x04, false],
      [0x05, true],
      [0xe1, false],
      [0x05, false],
    ]);
    k.releaseAll();
    expect(sent.length).toBe(6); // nothing more held
  });

  test("modifiers follow the event's flags when their own events never came", () => {
    const sent: [number, boolean][] = [];
    const k = new UsbKeyboard((u, d) => sent.push([u, d]));
    k.down(ev("Semicolon", { shiftKey: true })); // ':' with no Shift keydown first
    k.up(ev("Semicolon", { shiftKey: true }));
    k.down(ev("KeyA"));                          // Shift's release never came either
    expect(sent).toEqual([
      [0xe1, true],
      [0x33, true],
      [0x33, false],
      [0xe1, false],
      [0x04, true],
    ]);
    sent.length = 0;
    k.down(ev("ShiftRight", { shiftKey: true })); // a real right Shift: as is, no left one added
    k.down(ev("KeyB", { shiftKey: true }));
    expect(sent).toEqual([
      [0xe5, true],
      [0x05, true],
    ]);
  });
});
