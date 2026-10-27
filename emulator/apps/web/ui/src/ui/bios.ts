/**
 * The BIOS row in Settings: the one built into the page, or the owner's
 * own build of it (Load…), kept in this browser (localStorage) as the IIe
 * ROM is, and never uploaded. A BIOS is the machine's flash: loading one
 * restarts the page, which opens the machine on it (port/wasm-port.ts);
 * Use built-in forgets it and restarts on the page's own.
 */

import { MAX_BIOS } from "../port/wasm-port";
import { h, prefs } from "./dom";

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

/** Keep it for next time; false if the browser would not (then nothing is kept). */
export function keepBios(name: string, bytes: Uint8Array): boolean {
  const text = toBase64(bytes);
  prefs.set("bios", text);
  prefs.set("bios-name", name);
  if (prefs.get("bios") === text) return true;
  forgetBios();
  return false;
}

export function forgetBios(): void {
  prefs.remove("bios");
  prefs.remove("bios-name");
}

/** restart: start the page again (location.reload), on whatever is kept now. */
export function biosRow(restart: () => void) {
  const mine = keptBios();
  const picker = h("input", { type: "file", accept: ".rom,.bin", hidden: true, "data-testid": "bios-file" });
  const meta = h("div", { class: "meta", "data-testid": "bios-meta" });
  const load = h(
    "button",
    { class: "tbtn", type: "button", title: "Your own build of the BIOS (up to 512 KB, the flash): the machine restarts on it, and it stays in this browser", "data-testid": "bios-load" },
    "Load…",
  );
  const builtIn = h("button", { class: "tbtn", type: "button", hidden: !mine, title: "Forget yours, and restart on the page's own", "data-testid": "bios-builtin" }, "Use built-in");
  const note = h("p", { class: "row-note", role: "status", "data-testid": "bios-note" });
  const element = h("div", { class: "row-wrap" }, h("div", { class: "row" }, picker, h("div", { class: "what" }, h("div", { class: "name" }, "BIOS"), meta), builtIn, load), note);
  const say = (text: string) => (note.textContent = text);

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
    if (!keepBios(f.name, bytes)) return void say("This browser would not keep it, so the machine can't restart on it");
    say(`Restarting on ${f.name}…`);
    restart();
  });
  builtIn.addEventListener("click", () => {
    forgetBios();
    say("Restarting on the built-in BIOS…");
    restart();
  });
  show();

  /** The machine is open: the version its ROM greets with. */
  const connected = (v: string | undefined) => {
    version = v ?? "";
    show();
  };
  return { element, connected, mine };
}
