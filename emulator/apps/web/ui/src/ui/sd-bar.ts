/**
 * The SD card's row in Settings: the card's light (on while it works on a
 * command), its name and size, Protect (its write-protect switch), Save and
 * Eject; Insert… (an image of your own: whole 512-byte sectors, raw or FAT
 * as a PC formats it) while the slot is empty. And its pill in the status
 * strip (SD and the size, the light mirrored), which opens Settings at the
 * card. Shown when the machine has the card. C809R at the monitor lists
 * it, C80CR boots its /BOOT.BIN.
 *
 * The card stays in the page only: it is never uploaded, and not kept in
 * this browser either (a card is megabytes). Once the machine writes to it
 * the note says so, Save downloads it with the writes, and leaving the
 * page, Eject, or putting another card in asks first. A card over MAX_CARD
 * is refused before it is read: the core's memory stops at 2 GB.
 */

import type { FrameState, MachinePort, SdInfo } from "../port/machine-port";
import { h, passFocus } from "./dom";

/** The biggest card the page takes: the core holds it, and its memory stops at 2 GB. */
export const MAX_CARD = 1024 * 1024 * 1024;

/** "32 MB", "1440 KB": a card's size from its sectors. */
export function cardSize(sectors: number): string {
  const kb = sectors / 2;
  return kb >= 1024 && kb % 1024 === 0 ? `${kb / 1024} MB` : kb >= 10240 ? `${Math.round(kb / 1024)} MB` : `${kb} KB`;
}

export function sdBar(
  port: MachinePort,
  download: (name: string, bytes: Uint8Array) => void,
  win: Pick<Window, "addEventListener" | "confirm"> = window,
  hooks: { open?: () => void } = {},
) {
  const picker = h("input", { type: "file", hidden: true, "data-testid": "sd-file" });
  const light = h("span", { class: "led disk-led", title: "The SD card's light: on while it works", "data-testid": "sd-light" });
  const name = h("div", { class: "name", "data-testid": "sd-name" }, "The slot");
  const meta = h("div", { class: "meta", "data-testid": "sd-meta" }, "empty");
  const insert = h("button", { class: "tbtn", type: "button", "aria-label": "Insert an SD card", title: "A card image of your own (raw, or FAT as a PC formats it): it stays in this page", "data-testid": "sd-insert" }, "Insert…");
  const protect = h("button", { class: "tbtn", type: "button", "aria-pressed": "false", "aria-label": "Protect the SD card", title: "The card's write-protect switch", "data-testid": "sd-protect" }, "Protect");
  const save = h("button", { class: "tbtn", type: "button", "aria-label": "Save the SD card", title: "Download the card as it is now, with the machine's writes", "data-testid": "sd-save" }, "Save");
  const eject = h("button", { class: "tbtn", type: "button", "aria-label": "Eject the SD card", title: "Take the card out", "data-testid": "sd-eject" }, "Eject");
  const note = h("p", { class: "row-note", role: "status", "data-testid": "sd-note" });
  const element = h(
    "div",
    { class: "row-wrap", hidden: true, "data-testid": "sd-bar" },
    h("div", { class: "row" }, picker, light, h("div", { class: "what" }, name, meta), protect, save, eject, insert),
    note,
  );
  const pillLight = h("span", { class: "led disk-led" });
  const pillText = h("span", {});
  const pill = h("button", { class: "pill", type: "button", hidden: true, "data-testid": "pill-sd" }, pillLight, pillText);
  pill.addEventListener("click", () => hooks.open?.());

  let info: SdInfo | null = null;
  let present = false;
  const HINT = "C809R lists the card; C80CR boots its /BOOT.BIN";
  const say = (text: string) => (note.textContent = text);
  /** Unsaved writes: may they go? */
  const letGo = (doing: string) => !info?.changed || win.confirm(`${doing}? The machine's writes to ${info.name} aren't saved: Save keeps them.`);
  const show = () => {
    name.textContent = info ? info.name : "The slot";
    meta.textContent = info ? `${cardSize(info.sectors)}${info.changed ? " · changed since loaded" : ""}` : "empty";
    insert.hidden = !!info;
    protect.hidden = save.hidden = eject.hidden = !info;
    protect.setAttribute("aria-pressed", String(!!info?.writeProtected));
    protect.classList.toggle("on", !!info?.writeProtected);
    save.classList.toggle("on", !!info?.changed);
    pill.hidden = !present || !info;
    pillText.textContent = info ? `SD ${cardSize(info.sectors)}` : "";
    pill.title = info ? `The SD card: ${info.name}${info.changed ? ", written to" : ""}. Open in Settings` : "";
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
    if (info) passFocus(insert, eject);
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
    passFocus(eject, insert); // Eject is gone: the keyboard stays on the row
  });
  // Unsaved writes: ask before the page goes, and the card with it; unless the page asked already (waive).
  let waived = false;
  win.addEventListener("beforeunload", (e: Event) => {
    if (info?.changed && !waived) e.preventDefault();
  });
  show();

  const update = (f: FrameState) => {
    const has = port.caps.sd && (f.powered ? !!f.sd : present); // off, the frame has no card to say: as it was
    if (has !== present) {
      present = has;
      element.hidden = !present;
      show();
    }
    const busy = !!f.sd?.busy;
    light.classList.toggle("on", busy);
    pillLight.classList.toggle("on", busy);
    if (info && f.sd && f.sd.changed !== info.changed) {
      info = { ...info, changed: f.sd.changed };
      say(info.changed ? "Written to: Save to keep it, as the page forgets it" : "");
      show();
    }
  };
  return {
    element,
    pill,
    update,
    loaded: () => !!info,
    /** The card's name while it has writes not saved, else null. */
    unsaved: () => (info?.changed ? info.name : null),
    /** The page is going with the user's say-so: leaving doesn't ask again. */
    waive: () => (waived = true),
  };
}
