/**
 * The SD card's slot under the monitor: Insert SD card… (an image of your
 * own: whole 512-byte sectors, raw or FAT as a PC formats it), the card's
 * light (on while it works on a command), write-protect, Save card and
 * Eject. Shown when the machine has the card. C809R at the monitor lists
 * it, C80CR boots its /BOOT.BIN.
 *
 * The card stays in the page only: it is never uploaded, and not kept in
 * this browser either (a card is megabytes). Once the machine writes to it
 * the note says so, Save card downloads it with the writes, and leaving the
 * page, Eject, or putting another card in asks first. A card over MAX_CARD
 * is refused before it is read: the core's memory stops at 2 GB.
 */

import type { FrameState, MachinePort, SdInfo } from "../port/machine-port";
import { h } from "./dom";

/** The biggest card the page takes: the core holds it, and its memory stops at 2 GB. */
export const MAX_CARD = 1024 * 1024 * 1024;

/** "32 MB", "1440 KB": a card's size from its sectors. */
export function cardSize(sectors: number): string {
  const kb = sectors / 2;
  return kb >= 1024 && kb % 1024 === 0 ? `${kb / 1024} MB` : kb >= 10240 ? `${Math.round(kb / 1024)} MB` : `${kb} KB`;
}

export function sdBar(port: MachinePort, download: (name: string, bytes: Uint8Array) => void, win: Pick<Window, "addEventListener" | "confirm"> = window) {
  const picker = h("input", { type: "file", hidden: true, "data-testid": "sd-file" });
  const light = h("span", { class: "disk-led", title: "The SD card's light: on while it works", "data-testid": "sd-light" });
  const insert = h("button", { class: "tbtn", type: "button", "data-testid": "sd-insert" }, "Insert SD card…");
  const protect = h("button", { class: "tbtn", type: "button", "aria-pressed": "false", title: "The card's write-protect switch", "data-testid": "sd-protect" }, "Write-protect");
  const save = h("button", { class: "tbtn", type: "button", title: "Download the card as it is now, with the machine's writes", "data-testid": "sd-save" }, "Save card");
  const eject = h("button", { class: "tbtn", type: "button", title: "Take the card out", "data-testid": "sd-eject" }, "Eject");
  const note = h("span", { class: "apple-note", role: "status", "data-testid": "sd-note" });
  const element = h("div", { class: "apple-bar sd-bar", hidden: true, "data-testid": "sd-bar" }, picker, light, insert, protect, save, eject, note);

  let info: SdInfo | null = null;
  const HINT = "C809R lists the card; C80CR boots its /BOOT.BIN";
  const say = (text: string) => (note.textContent = text);
  /** Unsaved writes: may they go? */
  const letGo = (doing: string) => !info?.changed || win.confirm(`${doing}? The machine's writes to ${info.name} aren't saved: Save card keeps them.`);
  const show = () => {
    insert.textContent = info ? `${info.name} (${cardSize(info.sectors)})` : "Insert SD card…";
    insert.title = info ? `${info.name} is in the slot. Click to put in another.` : "A card image of your own (raw, or FAT as a PC formats it): it stays in this page";
    protect.hidden = save.hidden = eject.hidden = !info;
    protect.setAttribute("aria-pressed", String(!!info?.writeProtected));
    protect.classList.toggle("on", !!info?.writeProtected);
    save.classList.toggle("on", !!info?.changed);
  };

  insert.addEventListener("click", () => picker.click());
  picker.addEventListener("change", async () => {
    const f = picker.files?.[0];
    picker.value = "";
    if (!f) return;
    if (f.size > MAX_CARD) {
      say(`${f.name}: over 1 GB, more than this page can hold`);
      return;
    }
    if (!letGo(`Put ${f.name} in`)) return;
    try {
      info = await port.insertSd(new Uint8Array(await f.arrayBuffer()), f.name);
      say(HINT);
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
    }
    show();
  });
  protect.addEventListener("click", () => {
    if (!info) return;
    const on = !info.writeProtected;
    port.setSdWriteProtect(on);
    info = { ...info, writeProtected: on };
    show();
  });
  save.addEventListener("click", async () => {
    if (!info) return;
    try {
      download(info.name, await port.saveSd());
      info = { ...info, changed: false };
      say("");
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
    }
    show();
  });
  eject.addEventListener("click", () => {
    if (!letGo("Eject the card")) return;
    port.ejectSd();
    info = null;
    say("");
    show();
  });
  // Unsaved writes: ask before the page goes, and the card with it.
  win.addEventListener("beforeunload", (e: Event) => {
    if (info?.changed) e.preventDefault();
  });
  show();

  const update = (f: FrameState) => {
    element.hidden = !f.sd;
    light.classList.toggle("on", !!f.sd?.busy);
    if (info && f.sd && f.sd.changed !== info.changed) {
      info = { ...info, changed: f.sd.changed };
      say(info.changed ? "Written to: Save card to keep it, as the page forgets it" : "");
      show();
    }
  };
  return { element, update };
}
