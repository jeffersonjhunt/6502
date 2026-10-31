/**
 * Where a key on the screen goes (main.ts listens; F6, which leaves the
 * screen, is its own). First the joysticks, while their Keys switch is on
 * (ui/joysticks.ts); then the browser's paste chords; then the USB
 * keyboard on Display, or the serial terminal on Terminal. A key-up is the
 * joysticks' only if they took its press, else the USB keyboard's, so a
 * shortcut such as Ctrl+Left, which goes past the sticks, is let go too.
 *
 * While the left Shift is held as joystick 2's fire, the keyboards see the
 * other keys unshifted (keyboard.ts unshifted()): that Shift is a fire
 * button, not the typist's. The right Shift still shifts.
 */

import { isPaste, keyBytes, unshifted, type KeyLike } from "./keyboard";
import type { UsbKeyboard } from "./usb-keyboard";

/** As much of a KeyboardEvent as is read. */
export type ScreenKey = KeyLike & { code: string; shiftKey: boolean };

/** The joysticks' side (ui/joysticks.ts). */
export interface StickKeys {
  key(e: ScreenKey, down: boolean): boolean;
  shiftTaken(): boolean;
}

export function screenKeys(o: {
  sticks: StickKeys;
  usb: Pick<UsbKeyboard, "down" | "up">;
  /** The keyboard is the USB card's now (Display), not the terminal's. */
  onUsb: () => boolean;
  caps: () => boolean;
  serial: (bytes: Uint8Array) => void;
}) {
  return {
    /** @returns true if the page should not act on it too (preventDefault) */
    down(e: ScreenKey): boolean {
      if (o.sticks.key(e, true)) return true; // the Keys switch is on: a joystick's
      const k = e.shiftKey && o.sticks.shiftTaken() ? unshifted(e) : e;
      if (isPaste(k)) return false; // the browser's: its paste event types the text
      if (o.onUsb()) return o.usb.down(k);
      const bytes = keyBytes(k, o.caps());
      if (!bytes) return false;
      o.serial(bytes);
      return true;
    },
    up(e: ScreenKey): boolean {
      return o.sticks.key(e, false) || o.usb.up(e);
    },
  };
}
