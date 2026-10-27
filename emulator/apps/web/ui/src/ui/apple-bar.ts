/**
 * Apple II mode's ROMs in Settings, one row each: the owner's IIe ROM
 * (with Start Apple IIe), the character ROM, and the Disk II card's two
 * PROMs, each loaded and ejected on its own (ui/cartridge.ts identifies
 * them and keeps them in this browser); and the IIe's two resets, which
 * the status strip shows in Apple II mode. A part going in or out plugs a
 * new cartridge, which restarts the machine (a power cycle): the rows say
 * so, and in Apple II mode, where a IIe program would be lost, ask first.
 * Ejecting the IIe ROM keeps the other parts for the next one.
 *
 * Start Apple IIe is what the owner would do at the machine: START (a
 * hard reset, back to the BIOS whatever was running), then C806R at the
 * Woz Monitor. Reset (Ctrl-Reset) and Reboot (Ctrl-Open Apple-Reset) are
 * typed on the USB keyboard, Ctrl-Delete being its Reset key, so they work
 * exactly as the keys do.
 */

import type { Cartridge, FrameState, MachinePort } from "../port/machine-port";
import { forgetCartridge, forgetPart, forgetRom, keepCartridge, keptCartridge, keptParts, MAX_ROM_FILE, noRom, takePart, withoutPart, type RomFile, type RomPart, type RomParts } from "./cartridge";
import { h, passFocus } from "./dom";

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

/** What is missing from a cartridge that works, if anything. */
export function notes(c: Cartridge): string {
  const n: string[] = [];
  if (!c.chars) n.push("No character ROM: the BIOS draws one from the VGA font");
  if (!c.diskBoot && !c.diskSequencer) n.push("No Disk II PROMs: the drive needs its boot ROM and sequencer");
  else if (!c.diskBoot) n.push("No Disk II boot ROM (341-0027): the IIe will not boot from the drive");
  else if (!c.diskSequencer) n.push("No Disk II sequencer (341-0028): the drive will read nothing");
  return n.join(". ");
}

/** What booting from the drive still needs, or "" (the drive's hint). */
export function bootNeeds(c: Cartridge | null): string {
  if (!c) return "the IIe ROM and the Disk II PROMs";
  if (!c.diskBoot && !c.diskSequencer) return "the Disk II PROMs";
  if (!c.diskBoot) return "the Disk II boot ROM (341-0027)";
  if (!c.diskSequencer) return "the Disk II sequencer (341-0028)";
  return "";
}

/** "16 KB", "256 bytes". */
export function romSize(n: number): string {
  return n >= 1024 && n % 1024 === 0 ? `${n / 1024} KB` : `${n} bytes`;
}

export interface AppleBarHooks {
  /** Start Apple IIe was pressed: show the Apple's picture. */
  started: () => void;
  /** A reset was typed: the keyboard back to the screen, off the button. */
  typed?: () => void;
  /** Asks; true to go ahead (window.confirm). */
  confirm?: (question: string) => boolean;
}

interface PartRow {
  part: RomPart;
  name: string;
  /** The row's testids start with it. */
  id: string;
  /** What the row says with nothing in. */
  none: string;
}

const PARTS: PartRow[] = [
  { part: "rom", name: "Apple IIe ROM", id: "apple", none: "none: Apple II mode needs yours (16 KB, or the CD and EF halves)" },
  { part: "chars", name: "IIe character ROM", id: "apple-chars", none: "none: the built-in font" },
  { part: "diskBoot", name: "Disk II boot PROM", id: "apple-boot", none: "none: the IIe won't boot from the drive" },
  { part: "diskSequencer", name: "Disk II sequencer PROM", id: "apple-seq", none: "none: the drive reads nothing" },
];

export function appleBar(port: MachinePort, hooks: AppleBarHooks) {
  const note = h("p", { class: "row-note", role: "status", "data-testid": "apple-note" });
  const start = h("button", { class: "tbtn", type: "button", title: "START, then C806R: the BIOS hands over to the IIe ROM", "data-testid": "apple-start" }, "▶ Start Apple IIe");
  const reset = h("button", { class: "tbtn", type: "button", title: "The IIe's Ctrl-Reset (Ctrl-Delete): a BASIC program stays", "data-testid": "apple-reset" }, "Reset");
  const reboot = h("button", { class: "tbtn", type: "button", title: "Ctrl-Open Apple-Reset: the IIe starts afresh, booting from drive 1", "data-testid": "apple-reboot" }, "Reboot");
  const resets = h("span", { class: "strip-group", hidden: true, "data-testid": "apple-resets" }, reset, reboot);

  let current: Cartridge | null = null;
  let spare: RomParts = keptParts(); // the parts kept while no IIe ROM is in
  let kept = true;
  let apple = false;
  let powered = true;
  const say = (text: string) => (note.textContent = text);
  /** A new cartridge restarts the machine: in Apple II mode, may the IIe's program go? */
  const letGo = (doing: string) =>
    !apple || (hooks.confirm ?? ((q) => confirm(q)))(`${doing}? That restarts the machine: the IIe goes back to the BIOS, and what it runs now is lost.`);

  const plug = async (c: Cartridge | null): Promise<boolean> => {
    try {
      await port.insertCartridge(c);
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
      return false;
    }
    current = c;
    show();
    return true;
  };

  const rows = PARTS.map((p) => {
    const picker = h("input", { type: "file", multiple: p.part === "rom", hidden: true, "data-testid": `${p.id}-file` });
    const load = h("button", { class: "tbtn", type: "button", "aria-label": `Load the ${p.name}`, "data-testid": p.id === "apple" ? "apple-insert" : `${p.id}-load` }, "Load…");
    const eject = h(
      "button",
      {
        class: "tbtn",
        type: "button",
        "aria-label": `Eject the ${p.name}`,
        title:
          p.part === "rom"
            ? "Take the IIe ROM out, and forget it in this browser (the other parts are kept for the next): restarts the machine"
            : `Take the ${p.name} out: restarts the machine`,
        "data-testid": `${p.id}-eject`,
      },
      "Eject",
    );
    const meta = h("div", { class: "meta", "data-testid": `${p.id}-meta` });
    const element = h("div", { class: "row" }, picker, h("div", { class: "what" }, h("div", { class: "name" }, p.name), meta), ...(p.part === "rom" ? [start] : []), load, eject);

    load.addEventListener("click", () => picker.click());
    picker.addEventListener("change", async () => {
      const picked = Array.from(picker.files ?? []);
      picker.value = ""; // the same files again is a change too
      if (!picked.length) return;
      const big = picked.find((f) => f.size > MAX_ROM_FILE);
      if (big) return void say(noRom(big.name, big.size));
      const files: RomFile[] = [];
      for (const f of picked) {
        try {
          files.push({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) });
        } catch (err) {
          return void say(`${f.name}: ${err instanceof Error ? err.message : String(err)}`);
        }
      }
      const c = takePart(p.part, files, current, spare);
      if (typeof c === "string") return void say(c);
      if (!letGo(`Load ${picked[0].name}`)) return;
      if (!(await plug(c))) return;
      kept = keepCartridge(c);
      show();
      passFocus(load, eject);
      say(kept ? restarted(notes(c)) : "In for now; this browser would not keep it. The machine restarted with it");
    });
    eject.addEventListener("click", async () => {
      if (!current && p.part !== "rom") {
        spare = { ...spare, [p.part]: null }; // kept for a IIe ROM that isn't in: just forgotten
        forgetPart(p.part);
        show();
        return passFocus(eject, romLoad()); // its own Load waits for a IIe ROM
      }
      if (!letGo(`Eject the ${p.name}`)) return;
      const c = withoutPart(p.part, current);
      const was = current;
      if (!(await plug(c))) return;
      if (c) kept = keepCartridge(c);
      else {
        spare = { chars: was?.chars ?? null, diskBoot: was?.diskBoot ?? null, diskSequencer: was?.diskSequencer ?? null };
        if (spare.chars || spare.diskBoot || spare.diskSequencer) forgetRom();
        else forgetCartridge(); // nothing to keep: nothing left behind
      }
      show();
      passFocus(eject, load.disabled ? romLoad() : load);
      say(restarted(c ? notes(c) : spare.chars || spare.diskBoot || spare.diskSequencer ? "The other parts are kept for the next IIe ROM" : ""));
    });

    const update = () => {
      const waiting = !current && p.part !== "rom" ? spare[p.part] : null;
      const bytes = current?.[p.part] ?? waiting;
      meta.textContent = waiting
        ? `${romSize(waiting.length)} · yours, kept for the next IIe ROM`
        : bytes
          ? `${romSize(bytes.length)} · yours, ${kept ? "kept in this browser" : "in for now"}`
          : p.none;
      load.hidden = !!bytes;
      eject.hidden = !bytes;
      load.disabled = p.part !== "rom" && !current;
      load.title =
        p.part === "rom"
          ? "Your own Apple IIe ROM (16 KB, or the CD and EF halves; the character ROM and PROMs may come with it): it stays in this browser and is never uploaded. Restarts the machine"
          : current
            ? `Your ${p.name}: it stays in this browser. Restarts the machine`
            : "Load the Apple IIe ROM first";
    };
    return { element, update, load };
  });
  const romLoad = () => rows[0].load;

  /** The note after a new cartridge: the restart first. */
  const restarted = (n: string) => `The machine restarted${n ? `. ${n}` : ""}`;
  const show = () => {
    for (const r of rows) r.update();
    start.hidden = !current || apple;
    start.disabled = !powered;
    resets.hidden = !apple;
  };
  const element = h("div", { class: "rows", "data-testid": "apple-bar" }, ...rows.map((r) => r.element), note);

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
  return { element, resets, restore, update, bootNeeds: () => bootNeeds(current) };
}
