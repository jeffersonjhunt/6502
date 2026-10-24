/**
 * Apple II mode's controls under the monitor: the cartridge (the owner's
 * IIe ROM, which ui/cartridge.ts sorts and keeps in this browser), Start
 * Apple IIe, and the IIe's two resets.
 *
 * Start Apple IIe is what the owner would do at the machine: START (a
 * hard reset, back to the BIOS whatever was running), then C806R at the
 * Woz Monitor. Ctrl-Reset and Reboot (Ctrl-Open Apple-Reset) are typed on
 * the USB keyboard, Ctrl-Delete being its Reset key, so they work exactly
 * as the keys do.
 */

import type { Cartridge, FrameState, MachinePort } from "../port/machine-port";
import { forgetCartridge, keepCartridge, keptCartridge, sortRoms } from "./cartridge";
import { h } from "./dom";

const L_CTRL = 0xe0;
const L_GUI = 0xe3; // Open Apple
const DELETE = 0x4c;

/** Hold `mods`, tap Delete, let go: the IIe's Reset with them. */
export function typeReset(port: MachinePort, openApple: boolean): void {
  const mods = openApple ? [L_CTRL, L_GUI] : [L_CTRL];
  for (const k of mods) port.sendKey(k, true);
  port.sendKey(DELETE, true);
  port.sendKey(DELETE, false);
  for (const k of [...mods].reverse()) port.sendKey(k, false);
}

export function startApple(port: MachinePort): void {
  port.press("start");
  port.sendSerial(Uint8Array.from("C806R\r", (c) => c.charCodeAt(0)));
}

export interface AppleBarHooks {
  /** Start Apple IIe was pressed: show the Apple II scene. */
  started: () => void;
  /** A reset was typed: the keyboard back to the screen, off the button. */
  typed?: () => void;
}

export function appleBar(port: MachinePort, hooks: AppleBarHooks) {
  const picker = h("input", { type: "file", multiple: true, hidden: true, "data-testid": "apple-file" });
  const insert = h("button", { class: "tbtn", type: "button", "data-testid": "apple-insert" }, "Insert IIe ROM…");
  const eject = h("button", { class: "tbtn", type: "button", title: "Take the IIe ROM out, and forget it in this browser", "data-testid": "apple-eject" }, "Eject");
  const start = h("button", { class: "tbtn", type: "button", title: "START, then C806R: the BIOS hands over to the IIe ROM", "data-testid": "apple-start" }, "▶ Start Apple IIe");
  const reset = h("button", { class: "tbtn", type: "button", title: "Ctrl-Reset (Ctrl-Delete): the IIe's Reset; a BASIC program stays", "data-testid": "apple-reset" }, "Ctrl-Reset");
  const reboot = h("button", { class: "tbtn", type: "button", title: "Ctrl-Open Apple-Reset: the IIe starts afresh", "data-testid": "apple-reboot" }, "Reboot");
  const note = h("span", { class: "apple-note", role: "status", "data-testid": "apple-note" });
  const element = h("div", { class: "apple-bar", "data-testid": "apple-bar" }, picker, insert, eject, start, reset, reboot, note);

  let loaded = false;
  let apple = false;
  let powered = true;
  const show = () => {
    insert.textContent = loaded ? "IIe ROM ✓" : "Insert IIe ROM…";
    insert.title = loaded
      ? "Your Apple IIe ROM is in the cartridge, kept in this browser only. Click to put in another."
      : "Your own Apple IIe ROM (16 KB, or the CD and EF halves, and the character ROM): it stays in this browser and is never uploaded";
    insert.classList.toggle("on", loaded);
    eject.hidden = !loaded;
    start.hidden = !loaded || apple;
    start.disabled = !powered;
    reset.hidden = reboot.hidden = !apple;
  };
  const say = (text: string) => (note.textContent = text);

  const plug = async (c: Cartridge | null): Promise<boolean> => {
    try {
      await port.insertCartridge(c);
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
      return false;
    }
    loaded = c !== null;
    show();
    return true;
  };

  insert.addEventListener("click", () => picker.click());
  picker.addEventListener("change", async () => {
    const files = await Promise.all(Array.from(picker.files ?? [], async (f) => ({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) })));
    picker.value = ""; // the same files again is a change too
    if (!files.length) return;
    const c = sortRoms(files);
    if (typeof c === "string") return void say(c);
    if (!(await plug(c))) return;
    say(keepCartridge(c) ? (c.chars ? "" : "No character ROM: the BIOS draws one from the VGA font") : "In for now; this browser would not keep it");
  });
  eject.addEventListener("click", async () => {
    if (await plug(null)) {
      forgetCartridge();
      say("");
    }
  });
  start.addEventListener("click", () => {
    startApple(port);
    hooks.started();
  });
  reset.addEventListener("click", () => {
    typeReset(port, false);
    hooks.typed?.();
  });
  reboot.addEventListener("click", () => {
    typeReset(port, true);
    hooks.typed?.();
  });
  show();

  /** Once connected: the cartridge kept from last time goes back in. */
  const restore = async () => {
    if (!port.caps.cartridge) {
      element.hidden = true;
      return;
    }
    const c = keptCartridge();
    if (c) await plug(c);
  };
  const update = (f: FrameState) => {
    if (f.apple === apple && f.powered === powered) return;
    apple = f.apple;
    powered = f.powered;
    show();
  };
  return { element, restore, update };
}
