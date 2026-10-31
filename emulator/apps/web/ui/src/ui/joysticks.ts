/**
 * The serial card's two digital joysticks (hw/design.md, "The VIA and the
 * joystick ports"), from this computer: gamepads, and keys for those
 * without one.
 *
 * Gamepads (the Gamepad API): a pad, once the browser shows it, takes the
 * lowest free stick and keeps it (by Gamepad.index) until it disconnects;
 * then that stick is free for the next pad, a third one already there
 * included. A direction is the left stick past half way (DEAD_ZONE), so a
 * diagonal is two. A pad in the standard mapping also has its d-pad
 * (buttons 12-15) for directions and any of its four face buttons (0-3)
 * for fire, since a 2600 stick has one button and a pad's player presses
 * whichever; any other pad's buttons are numbered as its maker chose, so
 * only button 0 is fire. A browser shows a pad only once a button on it
 * is pressed.
 *
 * Keys, off by default: on Terminal and Display alike they would take the
 * arrows and Space from the serial and USB keyboards, so they do only
 * while Settings' Keys switch is on (remembered). Then joystick 1 is the
 * arrows and Space, joystick 2 W A S D and the left Shift, by physical key
 * (KeyboardEvent.code), whatever the layout. A stick's state is its pad's
 * and its keys' together. A key pressed with Ctrl, Alt or Meta is a
 * shortcut, the keyboards' and not a stick's. Who has a key is decided on
 * its first press and kept until it is let go: its repeats and its key-up
 * go to the same one, whatever Ctrl, Alt or Meta do meanwhile, so a held
 * arrow never moves between a stick and the USB keyboard, and no key is
 * left held on either. Two cases have no first press to go by: a repeat
 * of a key no one has (it was held while the page lost focus, which lets
 * go of everything) is taken as a press, a stick's unless Ctrl, Alt or
 * Meta is down; and a new press of a key someone still has means its
 * key-up went missing (macOS drops those for keys let go while Cmd is
 * held), so it is let go there and decided afresh. While the left Shift
 * is joystick 2's fire, the other keys reach the keyboards unshifted
 * (shiftTaken(); the right Shift still shifts).
 *
 * Settings' Joysticks section has the switch and a row a stick: the pad
 * driving it (or none) and a light while anything on it is pressed, which
 * a screen reader reads as what is pressed. The strip's JOY pill shows
 * while a pad is connected or the keys are on, and opens Settings there.
 * Only changes go to the port. Whether the machine has the ports is known
 * once it is open (caps.joysticks): connected() then shows or hides all
 * this, and sends what was pressed before.
 */

import { STICK, type MachinePort } from "../port/machine-port";
import { h, prefs } from "./dom";

/** How far the left stick goes before it is a direction. */
export const DEAD_ZONE = 0.5;

/** As much of a Gamepad as is read. */
export interface PadLike {
  readonly index: number;
  readonly id: string;
  readonly connected: boolean;
  /** "standard": the W3C layout, d-pad 12-15, face buttons 0-3. */
  readonly mapping?: string;
  readonly axes: readonly number[];
  readonly buttons: readonly { readonly pressed: boolean }[];
}

/** One pad's STICK bits. */
export function padBits(pad: PadLike): number {
  const b = (i: number) => !!pad.buttons[i]?.pressed;
  const x = pad.axes[0] ?? 0;
  const y = pad.axes[1] ?? 0;
  const standard = pad.mapping === "standard";
  const d = (i: number) => standard && b(i);
  let bits = 0;
  if (d(12) || y < -DEAD_ZONE) bits |= STICK.UP;
  if (d(13) || y > DEAD_ZONE) bits |= STICK.DOWN;
  if (d(14) || x < -DEAD_ZONE) bits |= STICK.LEFT;
  if (d(15) || x > DEAD_ZONE) bits |= STICK.RIGHT;
  if (b(0) || d(1) || d(2) || d(3)) bits |= STICK.FIRE;
  return bits;
}

/** Physical keys: [stick, STICK bit]. */
export const KEYS: Readonly<Record<string, readonly [number, number]>> = {
  ArrowUp: [0, STICK.UP],
  ArrowDown: [0, STICK.DOWN],
  ArrowLeft: [0, STICK.LEFT],
  ArrowRight: [0, STICK.RIGHT],
  Space: [0, STICK.FIRE],
  KeyW: [1, STICK.UP],
  KeyS: [1, STICK.DOWN],
  KeyA: [1, STICK.LEFT],
  KeyD: [1, STICK.RIGHT],
  ShiftLeft: [1, STICK.FIRE],
};

const NAMES: [number, string][] = [
  [STICK.UP, "up"],
  [STICK.DOWN, "down"],
  [STICK.LEFT, "left"],
  [STICK.RIGHT, "right"],
  [STICK.FIRE, "fire"],
];

/** "up fire", or "" for nothing pressed. */
export function describe(bits: number): string {
  return NAMES.filter(([b]) => bits & b)
    .map(([, n]) => n)
    .join(" ");
}

/** A pad's id is often "Name (STANDARD GAMEPAD Vendor: 045e Product: 02fd)": the name. */
export function padName(id: string): string {
  const name = id.replace(/\s*\(.*\)\s*$/, "").trim();
  return name || id || "Gamepad";
}

export function joysticks(
  port: MachinePort,
  pads: () => readonly (PadLike | null)[] = () => (typeof navigator !== "undefined" && navigator.getGamepads ? navigator.getGamepads() : []),
  hooks: { open?: (from: HTMLElement) => void } = {},
) {
  let keysOn = prefs.get("joy-keys") === "on";
  const keyBits = [0, 0];
  const padState = [0, 0];
  const sent = [0, 0];
  let names: (string | null)[] = [null, null];
  /** Each stick's pad, by Gamepad.index. */
  const slots: (number | null)[] = [null, null];
  /** Who has each held joystick key, from its first press to its key-up: true a stick, false the keyboards. */
  const owner = new Map<string, boolean>();
  let rightShift = false;

  const toggle = h(
    "button",
    {
      class: "tbtn",
      type: "button",
      "aria-pressed": "false",
      "aria-label": "Joystick keys",
      title: "Joystick 1 on the arrows and Space, joystick 2 on W A S D and the left Shift: they leave the terminal and the USB keyboard while on",
      "data-testid": "joy-keys",
    },
    "Keys",
  );
  const keysRow = h(
    "div",
    { class: "row" },
    h("div", { class: "what" }, h("div", { class: "name" }, "Keys"), h("div", { class: "meta", "data-testid": "joy-keys-meta" })),
    toggle,
  );
  const rows = [0, 1].map((n) => {
    const light = h("span", {
      class: "led disk-led",
      role: "img",
      "aria-label": `Joystick ${n + 1}: nothing pressed`,
      title: `Joystick ${n + 1}: on while anything on it is pressed`,
      "data-testid": `joy${n + 1}-light`,
    });
    const meta = h("div", { class: "meta", "data-testid": `joy${n + 1}-meta` });
    const element = h("div", { class: "row", "data-testid": `joy${n + 1}` }, light, h("div", { class: "what" }, h("div", { class: "name" }, `Joystick ${n + 1}`), meta));
    return { element, light, meta };
  });
  const note = h(
    "p",
    { class: "row-note" },
    "The serial card's ports: LDA $C341 is joystick 1, $C340 joystick 2, a bit 0 while pressed (up, down, left, right, fire). A gamepad shows here once a button on it is pressed.",
  );
  const element = h("div", { class: "row-wrap", hidden: !port.caps.joysticks, "data-testid": "joy-bar" }, rows[0].element, rows[1].element, keysRow, note);

  const pill = h("button", { class: "pill", type: "button", hidden: true, "data-testid": "pill-joy" });
  pill.addEventListener("click", () => hooks.open?.(pill));

  const send = () => {
    for (const n of [0, 1]) {
      const bits = keyBits[n] | padState[n];
      if (bits === sent[n]) continue;
      if (port.caps.joysticks) port.setJoystick(n, bits);
      sent[n] = bits;
      rows[n].light.classList.toggle("on", bits !== 0);
      rows[n].light.setAttribute("aria-label", `Joystick ${n + 1}: ${bits ? describe(bits).replaceAll(" ", ", ") : "nothing pressed"}`);
    }
  };
  const show = () => {
    toggle.setAttribute("aria-pressed", String(keysOn));
    toggle.classList.toggle("on", keysOn);
    (keysRow.querySelector('[data-testid="joy-keys-meta"]') as HTMLElement).textContent = keysOn
      ? "On: arrows and Space are joystick 1, W A S D and left Shift joystick 2"
      : "Off: the keyboard is the terminal's";
    for (const n of [0, 1]) {
      const by = [names[n] ? `Gamepad: ${names[n]}` : "No gamepad", ...(keysOn ? [n === 0 ? "arrows, Space" : "W A S D, left Shift"] : [])];
      rows[n].meta.textContent = by.join(" · ");
    }
    const on = [0, 1].filter((n) => names[n] || keysOn).map((n) => n + 1);
    pill.hidden = !port.caps.joysticks || on.length === 0;
    pill.textContent = `JOY ${on.join(" ")}`;
    pill.title = `Joysticks: ${[0, 1].map((n) => `${n + 1} ${names[n] ?? (keysOn ? "keys" : "none")}`).join(", ")}. Open in Settings`;
  };
  const releaseKeys = () => {
    keyBits[0] = keyBits[1] = 0;
    owner.clear();
    rightShift = false;
    send();
  };
  toggle.addEventListener("click", () => {
    keysOn = !keysOn;
    prefs.set("joy-keys", keysOn ? "on" : "off");
    if (!keysOn) releaseKeys();
    show();
  });

  /** A key on the screen: true if it was a joystick's (the caller keeps it from the keyboards). */
  const key = (e: Pick<KeyboardEvent, "code" | "ctrlKey" | "altKey" | "metaKey"> & { repeat?: boolean }, down: boolean): boolean => {
    if (e.code === "ShiftRight") rightShift = down; // the keyboards' always: it still shifts
    const k = KEYS[e.code];
    const had = owner.get(e.code);
    if (!down) {
      owner.delete(e.code);
      if (!had) return false; // its press went to the keyboards (or came before Keys): so does this
      keyBits[k[0]] &= ~k[1];
      send();
      return true;
    }
    if (had !== undefined) {
      if (e.repeat) return had; // a repeat goes where the first press went
      // A new press of a key still held: its key-up went missing (macOS drops
      // those for keys let go under Cmd). Let go of it, then decide afresh;
      // screen-keys has the USB keyboard let go of it if a stick takes it now.
      owner.delete(e.code);
      if (had) keyBits[k[0]] &= ~k[1];
    }
    // A repeat with no one holding it is of a key held from before (focus
    // lost and back, or Keys just on): the stick's from here, as a press.
    if (!k || !keysOn || !port.caps.joysticks) return false; // a lost key-up is only ever had while on
    const stick = !(e.ctrlKey || e.altKey || e.metaKey); // with one, a shortcut: the keyboards'
    owner.set(e.code, stick);
    if (stick) keyBits[k[0]] |= k[1];
    send();
    return stick;
  };

  /** Once a frame: read the pads. */
  const poll = () => {
    let list: readonly (PadLike | null)[] = [];
    try {
      list = pads() ?? [];
    } catch {
      list = []; // a browser that refuses (a permissions policy): no pads
    }
    const live = new Map<number, PadLike>();
    for (const p of list) if (p && p.connected) live.set(p.index, p);
    for (const n of [0, 1]) if (slots[n] !== null && !live.has(slots[n]!)) slots[n] = null; // gone: the stick is free
    for (const index of [...live.keys()].sort((a, b) => a - b)) {
      const free = slots.indexOf(null);
      if (free < 0) break;
      if (!slots.includes(index)) slots[free] = index; // a new pad: the lowest free stick
    }
    const pad = (n: number) => (slots[n] === null ? undefined : live.get(slots[n]!));
    const now = [0, 1].map((n) => (pad(n) ? padName(pad(n)!.id) : null));
    for (const n of [0, 1]) padState[n] = pad(n) ? padBits(pad(n)!) : 0;
    send();
    if (now[0] !== names[0] || now[1] !== names[1]) {
      names = now;
      show();
    }
  };
  /** The machine is open: caps.joysticks says whether it has the ports. */
  const connected = () => {
    element.hidden = !port.caps.joysticks;
    if (!port.caps.joysticks) releaseKeys();
    else for (const n of [0, 1]) if (sent[n]) port.setJoystick(n, sent[n]); // pressed before it started
    show();
  };
  show();
  return {
    element,
    pill,
    key,
    poll,
    releaseKeys,
    connected,
    /** The left Shift is joystick 2's fire now and the right Shift isn't held: other keys go unshifted. */
    shiftTaken: () => owner.get("ShiftLeft") === true && !rightShift,
    /** The Keys switch. */
    keysOn: () => keysOn,
    /** What each stick has pressed, as last sent. */
    state: () => [sent[0], sent[1]] as const,
  };
}
