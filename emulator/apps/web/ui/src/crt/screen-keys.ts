/**
 * Where a key on the screen goes (main.ts listens; F6, which leaves the
 * screen, is its own). First the joysticks, while their Keys switch is on
 * (ui/joysticks.ts); then the browser's paste chords; then the USB
 * keyboard on Display, or the serial terminal on Terminal. Who has a key
 * is decided on its first press and kept until its key-up: its repeats
 * and its key-up go there too, whatever Ctrl, Alt or Meta did meanwhile.
 * So a shortcut such as Ctrl+Left, which goes past the sticks, is let go
 * on the USB keyboard, and an arrow held as a stick stays the stick's.
 * A key held through a focus loss is the stick's from its next repeat,
 * and a key whose key-up went missing is decided afresh on its next
 * press (ui/joysticks.ts); if a stick takes a key the USB keyboard still
 * holds, the USB keyboard lets go of it.
 *
 * While the left Shift is held as joystick 2's fire, the keyboards see the
 * other keys unshifted (keyboard.ts unshifted()): that Shift is a fire
 * button, not the typist's. The right Shift still shifts.
 */

import { isPaste, keyBytes, unshifted, type KeyLike } from "./keyboard";
import type { UsbKeyboard } from "./usb-keyboard";

/** As much of a KeyboardEvent as is read. */
export type ScreenKey = KeyLike & { code: string; shiftKey: boolean; repeat?: boolean };

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
      if (o.sticks.key(e, true)) {
        o.usb.up(e); // a stick's now: if the USB keyboard still held it (its key-up went missing), let go
        return true;
      }
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
