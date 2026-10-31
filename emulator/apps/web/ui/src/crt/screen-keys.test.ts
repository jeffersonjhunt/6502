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
  const usbKeys = new UsbKeyboard((u, d) => usbSent.push([u, d]));
  const keys = screenKeys({
    sticks,
    usb: usbKeys,
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
  return { port, sticks, usbKeys, keys, ev, usbSent, serial, keysOn };
}

const SHIFT_L = 0xe1, SHIFT_R = 0xe5, CTRL_L = 0xe0, LEFT = 0x50, UP = 0x52, Q = 0x14, DIGIT1 = 0x1e;

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

  test("an arrow held as the stick's stays the stick's when Ctrl goes down mid-press: no USB key left held", () => {
    const { port, keys, ev, usbSent, keysOn, sticks } = make(true);
    keysOn();
    expect(keys.down(ev("ArrowLeft", "ArrowLeft"))).toBe(true);
    keys.down(ev("ControlLeft", "Control", { ctrlKey: true }));
    expect(keys.down(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true, repeat: true }))).toBe(true); // auto-repeat
    expect(keys.up(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true }))).toBe(true);
    keys.up(ev("ControlLeft", "Control"));
    expect(usbSent).toEqual([
      [CTRL_L, true],
      [CTRL_L, false],
    ]); // the arrow never reached it
    expect(port.callsTo("setJoystick")).toEqual([
      [0, STICK.LEFT],
      [0, 0],
    ]);
    expect(sticks.state()).toEqual([0, 0]);
  });

  test("Ctrl+arrow stays the USB keyboard's when Ctrl is let go mid-press: its up arrives, the stick never moves", () => {
    const { port, keys, ev, usbSent, keysOn } = make(true);
    keysOn();
    keys.down(ev("ControlLeft", "Control", { ctrlKey: true }));
    expect(keys.down(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true }))).toBe(true);
    keys.up(ev("ControlLeft", "Control"));
    expect(keys.down(ev("ArrowLeft", "ArrowLeft", { repeat: true }))).toBe(true); // auto-repeat, Ctrl up now
    expect(keys.up(ev("ArrowLeft", "ArrowLeft"))).toBe(true);
    expect(usbSent).toEqual([
      [CTRL_L, true],
      [LEFT, true],
      [CTRL_L, false],
      [LEFT, false],
    ]); // the repeat is the USB keyboard's too (it reports a held key once)
    expect(port.callsTo("setJoystick")).toEqual([]);
  });

  test("losing focus lets go of who had each key: the next press is decided afresh", () => {
    const { port, keys, ev, usbSent, keysOn, sticks, usbKeys } = make(true);
    keysOn();
    keys.down(ev("ControlLeft", "Control", { ctrlKey: true }));
    keys.down(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true }));
    usbKeys.releaseAll(); // main.ts on blur
    sticks.releaseKeys();
    usbSent.length = 0;
    expect(keys.down(ev("ArrowLeft", "ArrowLeft"))).toBe(true);
    expect(usbSent).toEqual([]);
    expect(port.callsTo("setJoystick")).toEqual([[0, STICK.LEFT]]);
  });

  for (const usb of [false, true])
    test(`a stick key held through a focus loss: its repeats after are the stick's, its key-up lets go (${usb ? "Display" : "Terminal"})`, () => {
      const { port, keys, ev, usbSent, serial, keysOn, sticks } = make(usb);
      keysOn();
      keys.down(ev("ArrowUp", "ArrowUp"));
      sticks.releaseKeys(); // main.ts on blur; focus back, the arrow still held
      expect(keys.down(ev("ArrowUp", "ArrowUp", { repeat: true }))).toBe(true);
      expect(keys.down(ev("ArrowUp", "ArrowUp", { repeat: true }))).toBe(true);
      expect(sticks.state()).toEqual([STICK.UP, 0]);
      expect(keys.up(ev("ArrowUp", "ArrowUp"))).toBe(true);
      expect(sticks.state()).toEqual([0, 0]);
      expect(usbSent).toEqual([]);
      expect(serial).toEqual([]);
      expect(port.callsTo("setJoystick")).toEqual([
        [0, STICK.UP],
        [0, 0],
        [0, STICK.UP],
        [0, 0],
      ]);
    });

  test("Cmd+Up's key-up went missing (macOS): the next plain Up is the stick's", () => {
    const { port, keys, ev, usbSent, keysOn, sticks } = make(true);
    keysOn();
    keys.down(ev("MetaLeft", "Meta", { metaKey: true }));
    expect(keys.down(ev("ArrowUp", "ArrowUp", { metaKey: true }))).toBe(false); // the browser's
    keys.up(ev("MetaLeft", "Meta")); // no ArrowUp up came
    expect(keys.down(ev("ArrowUp", "ArrowUp"))).toBe(true);
    expect(sticks.state()).toEqual([STICK.UP, 0]);
    expect(usbSent.filter(([u]) => u === UP)).toEqual([]);
    expect(port.callsTo("setJoystick")).toEqual([[0, STICK.UP]]);
  });

  test("a stick's key-up went missing: the same key with Meta is the keyboards', and the stick lets go", () => {
    const { port, keys, ev, keysOn, sticks } = make(true);
    keysOn();
    keys.down(ev("ArrowUp", "ArrowUp")); // no up came
    keys.down(ev("MetaLeft", "Meta", { metaKey: true }));
    expect(keys.down(ev("ArrowUp", "ArrowUp", { metaKey: true }))).toBe(false); // the browser's (Cmd+Up)
    expect(sticks.state()).toEqual([0, 0]);
    expect(port.callsTo("setJoystick")).toEqual([
      [0, STICK.UP],
      [0, 0],
    ]);
  });

  test("Ctrl+Left's key-up went missing: the next plain Left is the stick's, and the USB keyboard lets go of Left", () => {
    const { keys, ev, usbSent, keysOn, sticks } = make(true);
    keysOn();
    keys.down(ev("ControlLeft", "Control", { ctrlKey: true }));
    keys.down(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true }));
    keys.up(ev("ControlLeft", "Control")); // no ArrowLeft up came
    expect(keys.down(ev("ArrowLeft", "ArrowLeft"))).toBe(true);
    expect(sticks.state()).toEqual([STICK.LEFT, 0]);
    expect(usbSent).toEqual([
      [CTRL_L, true],
      [LEFT, true],
      [CTRL_L, false],
      [LEFT, false],
    ]);
    expect(keys.up(ev("ArrowLeft", "ArrowLeft"))).toBe(true);
    expect(sticks.state()).toEqual([0, 0]);
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

  test("joystick 2's fire on the left Shift with Caps Lock on: letters reach the terminal upper case, as unshifted", () => {
    const { keys, ev, serial, keysOn } = make(false);
    keysOn();
    const capsLock = { getModifierState: (m: string) => m === "CapsLock" };
    keys.down(ev("ShiftLeft", "Shift", { shiftKey: true, ...capsLock }));
    keys.down(ev("KeyQ", "q", { shiftKey: true, ...capsLock })); // Shift and Caps Lock: the browser says lower case
    keys.down(ev("Digit1", "!", { shiftKey: true, ...capsLock }));
    expect(String.fromCharCode(...serial)).toBe("Q1");
  });
});
