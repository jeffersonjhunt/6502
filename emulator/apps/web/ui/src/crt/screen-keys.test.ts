// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { STICK } from "../port/machine-port";
import { prefs } from "../ui/dom";
import { joysticks } from "../ui/joysticks";
import { screenKeys, type ScreenKey } from "./screen-keys";
import { UsbKeyboard } from "./usb-keyboard";

/** The screen's keys as main.ts wires them: real joysticks, a real USB keyboard, the terminal's bytes. */
function make(usb: boolean) {
  const port = new FakePort({ usb });
  const sticks = joysticks(port, () => []);
  const usbSent: [number, boolean][] = [];
  const serial: number[] = [];
  const keys = screenKeys({
    sticks,
    usb: new UsbKeyboard((u, d) => usbSent.push([u, d])),
    onUsb: () => usb,
    caps: () => false,
    serial: (b) => serial.push(...b),
  });
  const ev = (code: string, key: string, mods: Partial<ScreenKey> = {}): ScreenKey => ({
    code,
    key,
    ctrlKey: false,
    altKey: false,
    metaKey: false,
    shiftKey: false,
    getModifierState: () => false,
    ...mods,
  });
  const keysOn = () => sticks.element.querySelector<HTMLElement>('[data-testid="joy-keys"]')!.click();
  return { port, sticks, keys, ev, usbSent, serial, keysOn };
}

const SHIFT_L = 0xe1, SHIFT_R = 0xe5, CTRL_L = 0xe0, LEFT = 0x50, Q = 0x14, DIGIT1 = 0x1e;

describe("the screen's keys", () => {
  beforeEach(() => prefs.remove("joy-keys"));

  test("Ctrl+Left with Keys on is the USB keyboard's, down and up: nothing stays held", () => {
    const { keys, ev, usbSent, keysOn, sticks } = make(true);
    keysOn();
    expect(keys.down(ev("ControlLeft", "Control", { ctrlKey: true }))).toBe(true);
    expect(keys.down(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true }))).toBe(true);
    expect(keys.up(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true }))).toBe(true);
    expect(keys.up(ev("ControlLeft", "Control"))).toBe(true);
    expect(usbSent).toEqual([
      [CTRL_L, true],
      [LEFT, true],
      [LEFT, false],
      [CTRL_L, false],
    ]);
    expect(sticks.state()).toEqual([0, 0]);
  });

  test("a plain arrow with Keys on is the stick's both ways, and the USB keyboard never hears it", () => {
    const { port, keys, ev, usbSent, keysOn } = make(true);
    keysOn();
    expect(keys.down(ev("ArrowLeft", "ArrowLeft"))).toBe(true);
    expect(keys.up(ev("ArrowLeft", "ArrowLeft"))).toBe(true);
    expect(usbSent).toEqual([]);
    expect(port.callsTo("setJoystick")).toEqual([
      [0, STICK.LEFT],
      [0, 0],
    ]);
  });

  test("joystick 2's fire on the left Shift: the USB keyboard gets the other keys unshifted; the right Shift still shifts", () => {
    const { keys, ev, usbSent, keysOn, sticks } = make(true);
    keysOn();
    expect(keys.down(ev("ShiftLeft", "Shift", { shiftKey: true }))).toBe(true);
    expect(sticks.state()[1]).toBe(STICK.FIRE);
    keys.down(ev("KeyQ", "Q", { shiftKey: true }));
    keys.up(ev("KeyQ", "Q", { shiftKey: true }));
    expect(usbSent).toEqual([
      [Q, true],
      [Q, false],
    ]); // no Shift with it
    usbSent.length = 0;
    keys.down(ev("ShiftRight", "Shift", { shiftKey: true }));
    keys.down(ev("KeyQ", "Q", { shiftKey: true }));
    expect(usbSent).toEqual([
      [SHIFT_R, true],
      [Q, true],
    ]);
    keys.up(ev("KeyQ", "Q", { shiftKey: true }));
    keys.up(ev("ShiftRight", "Shift", { shiftKey: true }));
    keys.up(ev("ShiftLeft", "Shift"));
    expect(sticks.state()[1]).toBe(0);
    // Keys off: the left Shift is the keyboard's Shift again.
    keysOn();
    usbSent.length = 0;
    keys.down(ev("ShiftLeft", "Shift", { shiftKey: true }));
    keys.down(ev("Digit1", "!", { shiftKey: true }));
    expect(usbSent).toEqual([
      [SHIFT_L, true],
      [DIGIT1, true],
    ]);
  });

  test("joystick 2's fire on the left Shift: the terminal gets the other keys unshifted; the right Shift still shifts", () => {
    const { keys, ev, serial, keysOn } = make(false);
    keysOn();
    keys.down(ev("ShiftLeft", "Shift", { shiftKey: true }));
    keys.down(ev("KeyQ", "Q", { shiftKey: true }));
    keys.down(ev("Digit1", "!", { shiftKey: true }));
    keys.down(ev("Slash", "?", { shiftKey: true }));
    expect(String.fromCharCode(...serial)).toBe("q1/");
    serial.length = 0;
    keys.down(ev("ShiftRight", "Shift", { shiftKey: true }));
    keys.down(ev("KeyQ", "Q", { shiftKey: true }));
    keys.down(ev("Digit1", "!", { shiftKey: true }));
    expect(String.fromCharCode(...serial)).toBe("Q!");
    keys.up(ev("ShiftRight", "Shift", { shiftKey: true }));
    keys.up(ev("ShiftLeft", "Shift"));
    serial.length = 0;
    keys.down(ev("KeyQ", "Q", { shiftKey: true })); // a Shift the page never saw go down: the typist's
    expect(String.fromCharCode(...serial)).toBe("Q");
  });
});
