// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { STICK } from "../port/machine-port";
import { prefs } from "./dom";
import { DEAD_ZONE, describe as describeBits, joysticks, padBits, padName, type PadLike } from "./joysticks";

const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;

/** A mocked Gamepad in the standard mapping: `pressed` button numbers, the left stick at x, y. */
function pad(index: number, pressed: number[] = [], x = 0, y = 0, id = `Pad ${index} (STANDARD GAMEPAD Vendor: 045e Product: 02fd)`, mapping = "standard"): PadLike {
  return { index, id, connected: true, mapping, axes: [x, y, 0, 0], buttons: Array.from({ length: 17 }, (_, i) => ({ pressed: pressed.includes(i) })) };
}

function make(caps: { joysticks?: boolean } = {}) {
  const port = new FakePort(caps);
  const pads: (PadLike | null)[] = [];
  const opened: HTMLElement[] = [];
  const j = joysticks(port, () => pads, { open: (from) => opened.push(from) });
  document.body.replaceChildren(j.element, j.pill);
  const sent = () => port.callsTo("setJoystick");
  const key = (code: string, down: boolean, mods: Partial<KeyboardEvent> = {}) =>
    j.key({ code, ctrlKey: false, altKey: false, metaKey: false, ...mods }, down);
  return { port, pads, j, sent, key, opened };
}

describe("a gamepad's bits", () => {
  test("the d-pad and the left stick past the dead zone are directions; a diagonal is two", () => {
    expect(padBits(pad(0))).toBe(0);
    expect(padBits(pad(0, [12]))).toBe(STICK.UP);
    expect(padBits(pad(0, [13]))).toBe(STICK.DOWN);
    expect(padBits(pad(0, [14]))).toBe(STICK.LEFT);
    expect(padBits(pad(0, [15]))).toBe(STICK.RIGHT);
    expect(padBits(pad(0, [], 0, -1))).toBe(STICK.UP);
    expect(padBits(pad(0, [], 1, 1))).toBe(STICK.RIGHT | STICK.DOWN);
    expect(padBits(pad(0, [], -DEAD_ZONE, DEAD_ZONE))).toBe(0); // at the edge: not yet
    expect(padBits(pad(0, [], DEAD_ZONE, -DEAD_ZONE))).toBe(0);
    expect(padBits(pad(0, [], -0.3, 0.2))).toBe(0); // a stick at rest drifts
    expect(padBits(pad(0, [], 0.2, -0.3))).toBe(0);
    expect(padBits(pad(0, [], -0.6, 0))).toBe(STICK.LEFT);
  });

  test("any face button is fire; the others are not", () => {
    for (const b of [0, 1, 2, 3]) expect(padBits(pad(0, [b]))).toBe(STICK.FIRE);
    for (const b of [4, 5, 6, 7, 8, 9, 16]) expect(padBits(pad(0, [b]))).toBe(0);
    expect(padBits(pad(0, [0, 12, 15]))).toBe(STICK.FIRE | STICK.UP | STICK.RIGHT);
  });

  test("a pad not in the standard mapping: its left stick and button 0, nothing else", () => {
    const odd = (pressed: number[], x = 0, y = 0) => pad(0, pressed, x, y, "Odd Stick", "");
    for (const b of [1, 2, 3, 12, 13, 14, 15]) expect(padBits(odd([b]))).toBe(0); // its maker's numbering: unknown
    expect(padBits(odd([0]))).toBe(STICK.FIRE);
    expect(padBits(odd([], -1, 1))).toBe(STICK.LEFT | STICK.DOWN);
  });

  test("a pad with fewer buttons or axes reads what it has", () => {
    expect(padBits({ index: 0, id: "x", connected: true, axes: [1], buttons: [{ pressed: true }] })).toBe(STICK.RIGHT | STICK.FIRE);
  });

  test("names and states read plainly", () => {
    expect(padName("Xbox Wireless Controller (STANDARD GAMEPAD Vendor: 045e Product: 02fd)")).toBe("Xbox Wireless Controller");
    expect(padName("054c-09cc-Wireless Controller")).toBe("054c-09cc-Wireless Controller");
    expect(padName("")).toBe("Gamepad");
    expect(describeBits(STICK.UP | STICK.FIRE)).toBe("up fire");
    expect(describeBits(0)).toBe("");
  });
});

describe("the joysticks", () => {
  beforeEach(() => {
    prefs.remove("joy-keys");
    document.body.replaceChildren();
  });

  test("pads take the lowest free stick and keep it until they go; only changes are sent", () => {
    const { pads, j, sent } = make();
    j.poll();
    expect(sent()).toEqual([]); // nothing pressed, nothing to say
    pads[0] = pad(0, [12]);
    j.poll();
    expect(sent()).toEqual([[0, STICK.UP]]);
    j.poll();
    expect(sent()).toHaveLength(1); // the same again: nothing sent
    pads[1] = null; // a gap in the browser's list
    pads[2] = pad(2, [0]);
    pads[3] = pad(3, [13]); // a third pad: no joystick for it
    j.poll();
    expect(sent()).toEqual([
      [0, STICK.UP],
      [1, STICK.FIRE],
    ]);
    // Unplugged: joystick 1 is free, and the waiting third pad takes it; joystick 2 keeps its pad.
    pads[0] = null;
    j.poll();
    expect(sent().slice(2)).toEqual([[0, STICK.DOWN]]);
    expect($(j.element, "joy1-meta").textContent).toBe("Gamepad: Pad 3");
    expect($(j.element, "joy2-meta").textContent).toBe("Gamepad: Pad 2");
    // Pad 0 back: both sticks are taken, so it waits; pad 2 going frees joystick 2 for it.
    pads[0] = pad(0, [14]);
    j.poll();
    expect(sent()).toHaveLength(3);
    pads[2] = null;
    j.poll();
    expect(sent().slice(3)).toEqual([[1, STICK.LEFT]]);
    expect($(j.element, "joy2-meta").textContent).toBe("Gamepad: Pad 0");
  });

  test("a pad with a lower index than the one on joystick 1 does not take it over", () => {
    const { pads, j } = make();
    pads[3] = pad(3);
    j.poll();
    pads[1] = pad(1);
    j.poll();
    expect($(j.element, "joy1-meta").textContent).toBe("Gamepad: Pad 3");
    expect($(j.element, "joy2-meta").textContent).toBe("Gamepad: Pad 1");
  });

  test("Settings shows which pad drives each stick, a light while pressed, and the strip's pill", () => {
    const { pads, j, opened } = make();
    j.poll();
    expect($(j.element, "joy1-meta").textContent).toBe("No gamepad");
    expect($(j.element, "joy2-meta").textContent).toBe("No gamepad");
    expect(j.pill.hidden).toBe(true); // nothing connected, keys off
    pads[0] = pad(0, [], 0, 0, "Xbox Wireless Controller (STANDARD GAMEPAD Vendor: 045e Product: 02fd)");
    j.poll();
    expect($(j.element, "joy1-meta").textContent).toBe("Gamepad: Xbox Wireless Controller");
    expect(j.pill.hidden).toBe(false);
    expect(j.pill.textContent).toBe("JOY 1");
    expect($(j.element, "joy1-light").classList.contains("on")).toBe(false);
    pads[0] = pad(0, [1], 0, 0, "Xbox Wireless Controller");
    j.poll();
    expect($(j.element, "joy1-light").classList.contains("on")).toBe(true);
    j.pill.click();
    expect(opened).toEqual([j.pill]);
    pads[0] = { ...pad(0), connected: false }; // the browser says it went
    j.poll();
    expect($(j.element, "joy1-meta").textContent).toBe("No gamepad");
    expect(j.pill.hidden).toBe(true);
  });

  test("keys are off by default: the keyboard keeps every key", () => {
    const { j, key, sent } = make();
    expect(j.keysOn()).toBe(false);
    expect($(j.element, "joy-keys").getAttribute("aria-pressed")).toBe("false");
    for (const code of ["ArrowUp", "ArrowLeft", "Space", "KeyW", "ShiftLeft"]) {
      expect(key(code, true)).toBe(false);
      expect(key(code, false)).toBe(false);
    }
    expect(sent()).toEqual([]);
  });

  test("with Keys on, the arrows and Space are joystick 1, W A S D and left Shift joystick 2; remembered", () => {
    const { j, key, sent } = make();
    $(j.element, "joy-keys").click();
    expect(j.keysOn()).toBe(true);
    expect(prefs.get("joy-keys")).toBe("on");
    expect($(j.element, "joy-keys").getAttribute("aria-pressed")).toBe("true");
    expect(j.pill.hidden).toBe(false);
    expect(j.pill.textContent).toBe("JOY 1 2");
    expect($(j.element, "joy1-meta").textContent).toBe("No gamepad · arrows, Space");
    expect(key("ArrowUp", true)).toBe(true);
    expect(key("Space", true)).toBe(true);
    expect(key("KeyD", true)).toBe(true);
    expect(key("ShiftLeft", true)).toBe(true);
    expect(j.state()).toEqual([STICK.UP | STICK.FIRE, STICK.RIGHT | STICK.FIRE]);
    expect(key("ArrowUp", false)).toBe(true);
    expect(j.state()[0]).toBe(STICK.FIRE);
    expect(key("KeyQ", true)).toBe(false); // not a stick's: the keyboard's
    expect(key("ArrowLeft", true, { ctrlKey: true })).toBe(false); // a shortcut
    expect(key("ArrowLeft", false)).toBe(false); // ...and its key-up goes with it
    expect(sent()).toEqual([
      [0, STICK.UP],
      [0, STICK.UP | STICK.FIRE],
      [1, STICK.RIGHT],
      [1, STICK.RIGHT | STICK.FIRE],
      [0, STICK.FIRE],
    ]);
    j.releaseKeys(); // the screen lost focus: nothing stays held
    expect(j.state()).toEqual([0, 0]);
    // A new page: still on.
    const again = make();
    expect(again.j.keysOn()).toBe(true);
  });

  test("a stick takes the key-ups of its own presses only", () => {
    const { j, key, sent } = make();
    $(j.element, "joy-keys").click();
    expect(key("ArrowDown", false)).toBe(false); // pressed before Keys went on: the keyboard's
    expect(key("ArrowDown", true)).toBe(true);
    expect(key("ArrowDown", true)).toBe(true); // the host's auto-repeat
    expect(key("ArrowDown", false)).toBe(true);
    expect(key("ArrowDown", false)).toBe(false);
    expect(sent()).toEqual([
      [0, STICK.DOWN],
      [0, 0],
    ]);
  });

  test("the left Shift as fire is taken from the keyboards' shifting, unless the right Shift is held", () => {
    const { j, key } = make();
    expect(j.shiftTaken()).toBe(false);
    key("ShiftLeft", true);
    expect(j.shiftTaken()).toBe(false); // Keys off: the left Shift is the keyboards'
    key("ShiftLeft", false);
    $(j.element, "joy-keys").click();
    key("ShiftLeft", true);
    expect(j.shiftTaken()).toBe(true);
    key("ShiftRight", true);
    expect(j.shiftTaken()).toBe(false);
    key("ShiftRight", false);
    expect(j.shiftTaken()).toBe(true);
    key("ShiftLeft", false);
    expect(j.shiftTaken()).toBe(false);
    key("ShiftLeft", true);
    j.releaseKeys();
    expect(j.shiftTaken()).toBe(false);
  });

  test("each light says what is pressed; the switch is named for what it does", () => {
    const { pads, j, key } = make();
    const light = $(j.element, "joy1-light");
    expect(light.getAttribute("role")).toBe("img");
    expect(light.getAttribute("aria-label")).toBe("Joystick 1: nothing pressed");
    expect($(j.element, "joy2-light").getAttribute("aria-label")).toBe("Joystick 2: nothing pressed");
    expect($(j.element, "joy-keys").getAttribute("aria-label")).toBe("Joystick keys");
    pads[0] = pad(0, [12, 0]);
    j.poll();
    expect(light.getAttribute("aria-label")).toBe("Joystick 1: up, fire");
    $(j.element, "joy-keys").click();
    key("KeyA", true);
    expect($(j.element, "joy2-light").getAttribute("aria-label")).toBe("Joystick 2: left");
    pads[0] = pad(0);
    j.poll();
    expect(light.getAttribute("aria-label")).toBe("Joystick 1: nothing pressed");
  });

  test("connected(): a machine without the ports hides them and takes no keys; with them, what was held before is sent", () => {
    const before = make({ joysticks: false });
    before.pads[0] = pad(0, [0]);
    before.j.poll(); // a pad held while the machine starts
    expect(before.sent()).toEqual([]);
    before.port.caps.joysticks = true; // open: it has them
    before.j.connected();
    expect(before.j.element.hidden).toBe(false);
    expect(before.sent()).toEqual([[0, STICK.FIRE]]);

    const none = make({ joysticks: true });
    $(none.j.element, "joy-keys").click();
    none.port.caps.joysticks = false; // open: a machine without the ports
    none.j.connected();
    expect(none.j.element.hidden).toBe(true);
    expect(none.j.pill.hidden).toBe(true);
    expect(none.key("ArrowUp", true)).toBe(false); // the keyboard's
    none.pads[0] = pad(0, [12]);
    none.j.poll();
    expect(none.sent()).toEqual([]);
  });

  test("a stick is its pad and its keys together; Keys off lets the keys go", () => {
    const { pads, j, key } = make();
    $(j.element, "joy-keys").click();
    pads[0] = pad(0, [0]);
    j.poll();
    key("ArrowLeft", true);
    expect(j.state()[0]).toBe(STICK.FIRE | STICK.LEFT);
    $(j.element, "joy-keys").click(); // off
    expect(prefs.get("joy-keys")).toBe("off");
    expect(j.state()[0]).toBe(STICK.FIRE); // the pad's fire still held
    expect(key("ArrowLeft", false)).toBe(false);
  });

  test("a machine without the ports: hidden, nothing sent; a browser without the Gamepad API: no pads", () => {
    const { pads, j, sent } = make({ joysticks: false });
    expect(j.element.hidden).toBe(true);
    pads[0] = pad(0, [12]);
    j.poll();
    expect(sent()).toEqual([]);
    expect(j.pill.hidden).toBe(true);
    const port = new FakePort();
    const none = joysticks(port, () => {
      throw new Error("blocked by a permissions policy");
    });
    expect(() => none.poll()).not.toThrow();
    expect(port.callsTo("setJoystick")).toEqual([]);
  });
});
