/**
 * The BIOS row in Settings: the one built into the page, or the owner's
 * own build of it (Load…), kept in this browser (localStorage) as the IIe
 * ROM is, and never uploaded. A BIOS is the machine's flash: loading one
 * restarts the page, which opens the machine on it (port/wasm-port.ts);
 * Use built-in forgets it and restarts on the page's own. When a restart
 * would lose something (the SD card's writes, a drive at work), it asks
 * first, in the page, and only then keeps or forgets: so the page's own
 * prompt on leaving can't stop a restart the choice was already saved for.
 *
 * Yours that prints nothing for QUIET_MS gets a hint in the status strip
 * (quiet), pointing to Use built-in.
 */

import { MAX_BIOS } from "../port/wasm-port";
import { h, passFocus, prefs } from "./dom";

export interface KeptBios {
  name: string;
  bytes: Uint8Array;
}

function toBase64(b: Uint8Array): string {
  let s = "";
  for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode(...b.subarray(i, i + 0x8000));
  return btoa(s);
}

/** The owner's BIOS kept from last time, if any. */
export function keptBios(): KeptBios | null {
  const text = prefs.get("bios");
  if (!text) return null;
  try {
    const bytes = Uint8Array.from(atob(text), (c) => c.charCodeAt(0));
    return bytes.length && bytes.length <= MAX_BIOS ? { name: prefs.get("bios-name") || "bios.rom", bytes } : null;
  } catch {
    return null;
  }
}

/** Keep it for next time; false if the browser would not (then what was kept before stays). */
export function keepBios(name: string, bytes: Uint8Array): boolean {
  const text = toBase64(bytes);
  const was = { bios: prefs.get("bios"), "bios-name": prefs.get("bios-name") };
  prefs.set("bios", text);
  prefs.set("bios-name", name);
  if (prefs.get("bios") === text && prefs.get("bios-name") === name) return true;
  for (const [key, value] of Object.entries(was)) {
    if (value === null) prefs.remove(key);
    else if (prefs.get(key) !== value) prefs.set(key, value);
  }
  return false;
}

export function forgetBios(): void {
  prefs.remove("bios");
  prefs.remove("bios-name");
}

/** How long a BIOS of the owner's may stay silent on the serial line before the strip points the way back. */
export const QUIET_MS = 5000;

/** Where the way back to the page's own BIOS is. */
export function yoursLoaded(mine: KeptBios): string {
  return `Your BIOS, ${mine.name}, is loaded: Settings › ROMs › Use built-in`;
}

export interface BiosHooks {
  /** What a restart now would lose, as phrases ("the SD card's writes..."); none: nothing. */
  atRisk?: () => string[];
  /** Asks; true to go ahead (window.confirm). */
  confirm?: (question: string) => boolean;
  /** Agreed: the page goes without asking again. */
  leaving?: () => void;
  /** The quiet hint's link was clicked (from it): show the BIOS row in Settings. */
  open?: (from: HTMLElement) => void;
}

/** restart: start the page again (location.reload), on whatever is kept now. */
export function biosRow(restart: () => void, hooks: BiosHooks = {}) {
  const mine = keptBios();
  const picker = h("input", { type: "file", accept: ".rom,.bin", hidden: true, "data-testid": "bios-file" });
  const meta = h("div", { class: "meta", "data-testid": "bios-meta" });
  const load = h(
    "button",
    { class: "tbtn", type: "button", "aria-label": "Load a BIOS of your own", title: "Your own build of the BIOS (up to 512 KB, the flash): the machine restarts on it, and it stays in this browser", "data-testid": "bios-load" },
    "Load…",
  );
  const builtIn = h("button", { class: "tbtn", type: "button", hidden: !mine, title: "Forget yours, and restart on the page's own", "data-testid": "bios-builtin" }, "Use built-in");
  const note = h("p", { class: "row-note", role: "status", "data-testid": "bios-note" });
  const element = h("div", { class: "row-wrap" }, h("div", { class: "row" }, picker, h("div", { class: "what" }, h("div", { class: "name" }, "BIOS"), meta), builtIn, load), note);
  const say = (text: string) => (note.textContent = text);
  /** May the machine restart, losing what it would? */
  const letGo = (doing: string) => {
    const risks = hooks.atRisk?.() ?? [];
    return !risks.length || (hooks.confirm ?? ((q) => confirm(q)))(`${doing}? The machine restarts: ${risks.join("; ")}.`);
  };

  let version = "";
  const show = () => {
    const v = version ? ` ${version}` : "";
    meta.textContent = mine ? `yours: ${mine.name}${v ? ` ·${v}` : ""} · kept in this browser` : `built-in${v} · or load your own build`;
  };

  load.addEventListener("click", () => picker.click());
  picker.addEventListener("change", async () => {
    const f = picker.files?.[0];
    picker.value = "";
    if (!f) return;
    if (!f.size || f.size > MAX_BIOS) return void say(`${f.name}: ${f.size} bytes is no BIOS for the flash (up to 512 KB)`);
    let bytes: Uint8Array;
    try {
      bytes = new Uint8Array(await f.arrayBuffer());
    } catch (err) {
      return void say(`${f.name}: ${err instanceof Error ? err.message : String(err)}`);
    }
    if (!letGo(`Restart on ${f.name}`)) return;
    if (!keepBios(f.name, bytes)) return void say("This browser would not keep it, so the machine can't restart on it");
    hooks.leaving?.();
    say(`Restarting on ${f.name}…`);
    restart();
  });
  builtIn.addEventListener("click", () => {
    if (!letGo("Restart on the built-in BIOS")) return;
    forgetBios();
    hooks.leaving?.();
    builtIn.hidden = true; // nothing of yours kept now
    passFocus(builtIn, load);
    say("Restarting on the built-in BIOS…");
    restart();
  });
  show();

  // Yours, silent: the strip says where Use built-in is.
  const way = h("button", { class: "linkish", type: "button", "data-testid": "bios-quiet-open" }, "Settings › ROMs › Use built-in");
  way.addEventListener("click", () => hooks.open?.(way));
  const quiet = h("span", { class: "strip-empty", role: "status", hidden: true, "data-testid": "bios-quiet" }, mine ? `Your BIOS, ${mine.name}, has printed nothing: ` : "", way);
  let heard = false;

  /** The machine is open: the version its ROM greets with. */
  const connected = (v: string | undefined) => {
    version = v ?? "";
    show();
    if (mine) setTimeout(() => (quiet.hidden = heard), QUIET_MS);
  };
  /** The serial line said something. */
  const spoke = () => {
    if (heard) return;
    heard = true;
    quiet.hidden = true;
  };
  return { element, connected, mine, quiet, spoke };
}
