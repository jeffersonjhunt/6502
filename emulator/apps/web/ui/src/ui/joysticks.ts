/**
 * The serial card's two digital joysticks (hw/design.md, "The VIA and the
 * joystick ports"), from this computer: gamepads, and keys for those
 * without one.
 *
 * Gamepads (the Gamepad API): the first two connected, in the browser's
 * order, are joysticks 1 and 2. A direction is the d-pad (buttons 12-15 in
 * the standard mapping) or the left stick past half way (DEAD_ZONE), so a
 * diagonal is two; fire is any of the four face buttons (0-3), since a
 * 2600 stick has one button and a pad's player presses whichever. A
 * browser shows a pad only once a button on it is pressed.
 *
 * Keys, off by default: on Terminal and Display alike they would take the
 * arrows and Space from the serial and USB keyboards, so they do only
 * while Settings' Keys switch is on (remembered). Then joystick 1 is the
 * arrows and Space, joystick 2 W A S D and the left Shift, by physical key
 * (KeyboardEvent.code), whatever the layout. A stick's state is its pad's
 * and its keys' together.
 *
 * Settings' Joysticks section has the switch and a row a stick: the pad
 * driving it (or none) and a light while anything on it is pressed. The
 * strip's JOY pill shows while a pad is connected or the keys are on, and
 * opens Settings there. Only changes go to the port.
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
  readonly axes: readonly number[];
  readonly buttons: readonly { readonly pressed: boolean }[];
}

/** One pad's STICK bits. */
export function padBits(pad: PadLike): number {
  const b = (i: number) => !!pad.buttons[i]?.pressed;
  const x = pad.axes[0] ?? 0;
  const y = pad.axes[1] ?? 0;
  let bits = 0;
  if (b(12) || y < -DEAD_ZONE) bits |= STICK.UP;
  if (b(13) || y > DEAD_ZONE) bits |= STICK.DOWN;
  if (b(14) || x < -DEAD_ZONE) bits |= STICK.LEFT;
  if (b(15) || x > DEAD_ZONE) bits |= STICK.RIGHT;
  if (b(0) || b(1) || b(2) || b(3)) bits |= STICK.FIRE;
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

  const toggle = h(
    "button",
    {
      class: "tbtn",
      type: "button",
      "aria-pressed": "false",
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
    const light = h("span", { class: "led disk-led", title: `Joystick ${n + 1}: on while anything on it is pressed`, "data-testid": `joy${n + 1}-light` });
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
      if (bits !== sent[n] && port.caps.joysticks) port.setJoystick(n, bits);
      sent[n] = bits;
      rows[n].light.classList.toggle("on", bits !== 0);
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
    send();
  };
  toggle.addEventListener("click", () => {
    keysOn = !keysOn;
    prefs.set("joy-keys", keysOn ? "on" : "off");
    if (!keysOn) releaseKeys();
    show();
  });

  /** A key on the screen: true if it was a joystick's (the caller keeps it from the keyboards). */
  const key = (e: Pick<KeyboardEvent, "code" | "ctrlKey" | "altKey" | "metaKey">, down: boolean): boolean => {
    if (!keysOn || !port.caps.joysticks) return false;
    const k = KEYS[e.code];
    if (!k || (down && (e.ctrlKey || e.altKey || e.metaKey))) return false; // a shortcut, not a stick
    const [n, bit] = k;
    keyBits[n] = down ? keyBits[n] | bit : keyBits[n] & ~bit;
    send();
    return true;
  };

  /** Once a frame: read the pads. */
  const poll = () => {
    let list: readonly (PadLike | null)[] = [];
    try {
      list = pads() ?? [];
    } catch {
      list = []; // a browser that refuses (a permissions policy): no pads
    }
    const live = list.filter((p): p is PadLike => !!p && p.connected).sort((a, b) => a.index - b.index);
    const now = [0, 1].map((n) => (live[n] ? padName(live[n].id) : null));
    for (const n of [0, 1]) padState[n] = live[n] ? padBits(live[n]) : 0;
    send();
    if (now[0] !== names[0] || now[1] !== names[1]) {
      names = now;
      show();
    }
  };
  show();
  return {
    element,
    pill,
    key,
    poll,
    releaseKeys,
    /** The Keys switch. */
    keysOn: () => keysOn,
    /** What each stick has pressed, as last sent. */
    state: () => [sent[0], sent[1]] as const,
  };
}
