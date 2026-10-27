/**
 * A Disk II drive's row in Settings: its light (on while its motor runs),
 * the disk's name, size and format, Protect (the write-protect notch),
 * Save and Eject; Insert… (.woz, .dsk, .do, .po) while it is empty. And
 * its pill in the status strip (D1 or D2, the light mirrored), which opens
 * Settings at the drives. Shown when the machine has the card.
 *
 * The disk is kept in this browser (ui/disk-store.ts) as it went in, and
 * again whenever the motor stops after it was written to: in the format it
 * came in while it still fits, else as WOZ. Save downloads it the same way,
 * saying so when a sector image had to become a .woz.
 *
 * A disk going in boots nothing by itself, as on a real IIe, so until the
 * drive first spins with it drive 1's note says what will: the PROMs, if
 * bootNeeds() says they are missing; Start Apple IIe; or Reboot.
 */

import type { DiskFormat, DiskInfo, FrameState, MachinePort } from "../port/machine-port";
import type { DiskStore, KeptDisk } from "./disk-store";
import { h } from "./dom";

const ACCEPT = ".woz,.dsk,.do,.po";
/** A 5.25-inch disk's image is a few hundred KB, a few MB as a WOZ with flux tracks: bigger is a wrong file. */
const MAX_DISK = 16 * 1024 * 1024;
const FORMATS: Record<DiskFormat, string> = { woz: "WOZ", dsk: "DOS order", po: "ProDOS order" };

/** The name a disk is saved under in a format: the same stem, that format's extension. */
export function savedName(name: string, format: DiskFormat): string {
  const stem = name.replace(/\.(woz|dsk|do|po)$/i, "") || "disk";
  return `${stem}.${format}`;
}

/** "140 KB", "1.2 MB". */
export function diskSize(n: number): string {
  return n >= 1024 * 1024 ? `${(n / (1024 * 1024)).toFixed(1)} MB` : `${Math.round(n / 1024)} KB`;
}

export interface DiskBarHooks {
  /** What booting from the drive still needs ("" for nothing): drive 1's hint. */
  bootNeeds?: () => string;
  /** The pill was clicked: show this drive in Settings. */
  open?: () => void;
}

/** drive: 0 or 1. Drive 1's testids are disk-*, drive 2's disk-*-2. */
export function diskBar(port: MachinePort, store: DiskStore, download: (name: string, bytes: Uint8Array) => void, hooks: DiskBarHooks = {}, drive = 0) {
  const id = (what: string) => `disk-${what}${drive ? "-2" : ""}`;
  const label = `Drive ${drive + 1}`;
  const bootNeeds = hooks.bootNeeds ?? (() => "");
  const picker = h("input", { type: "file", accept: ACCEPT, hidden: true, "data-testid": id("file") });
  const light = h("span", { class: "led disk-led", title: `${label}'s light: on while its motor runs`, "data-testid": id("light") });
  const name = h("div", { class: "name", "data-testid": id("name") }, label);
  const meta = h("div", { class: "meta", "data-testid": id("meta") }, "empty");
  const insert = h("button", { class: "tbtn", type: "button", title: "A .woz, .dsk, .do or .po of your own: it stays in this browser", "data-testid": id("insert") }, "Insert…");
  const protect = h("button", { class: "tbtn", type: "button", "aria-pressed": "false", title: "The disk's write-protect notch", "data-testid": id("protect") }, "Protect");
  const save = h("button", { class: "tbtn", type: "button", title: "Download the disk as it is now", "data-testid": id("save") }, "Save");
  const eject = h("button", { class: "tbtn", type: "button", title: "Take the disk out, and forget it in this browser", "data-testid": id("eject") }, "Eject");
  const note = h("p", { class: "row-note", role: "status", "data-testid": id("note") });
  const element = h(
    "div",
    { class: "row-wrap", hidden: true, "data-testid": id("bar") },
    h("div", { class: "row" }, picker, light, h("div", { class: "what" }, name, meta), protect, save, eject, insert),
    note,
  );
  const pillLight = h("span", { class: "led disk-led" });
  const pillText = h("span", {});
  const pill = h("button", { class: "pill", type: "button", hidden: true, "data-testid": `pill-d${drive + 1}` }, pillLight, pillText);
  pill.addEventListener("click", () => hooks.open?.());

  let info: DiskInfo | null = null;
  let size = 0;
  let picked = false; // set as the change comes in: the user's disk wins over the kept one
  let kept: KeptDisk | null = null;
  let keptWrites = 0;
  let spinning = false;
  let keeping = 0; // keeps of what was written, still going
  let read = false; // the drive has spun with this disk in
  let auto = ""; // the note update() last put up
  let present = false; // the machine has the card
  let apple = false;
  const WAITING = "No disk: the IIe waits for one, as a real one does. Reset for BASIC.";
  const say = (text: string) => (note.textContent = text);
  const show = () => {
    name.textContent = info ? `${label} · ${info.name}` : label;
    meta.textContent = info ? `${diskSize(size)} · ${FORMATS[info.format]}` : "empty";
    insert.hidden = !!info;
    protect.hidden = save.hidden = eject.hidden = !info;
    protect.setAttribute("aria-pressed", String(!!info?.writeProtected));
    protect.classList.toggle("on", !!info?.writeProtected);
    // The pill: a disk in; in Apple II mode, where the drives matter, an empty one too.
    pill.hidden = !present || (!info && !apple);
    pillText.textContent = `D${drive + 1} ${info ? info.name : "empty"}`;
    pill.title = info ? `${label}: ${info.name}. Open in Settings` : `${label} is empty: insert a disk in Settings`;
  };

  const put = async (disk: KeptDisk, bytes: Uint8Array): Promise<DiskInfo | null> => {
    let got: DiskInfo;
    try {
      got = await port.insertDisk(drive, bytes, disk.name);
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
      return null;
    }
    if (disk.writeProtected) port.setWriteProtect(drive, true);
    info = { ...got, writeProtected: disk.writeProtected };
    size = bytes.length;
    kept = disk;
    keptWrites = info.writes;
    read = false;
    return info;
  };

  /** The disk as a file: its own format if it still fits, else WOZ. */
  const file = async (format: DiskFormat): Promise<{ bytes: Uint8Array; format: DiskFormat }> => {
    try {
      return { bytes: await port.saveDisk(drive, format), format };
    } catch (err) {
      if (format === "woz") throw err;
      return { bytes: await port.saveDisk(drive, "woz"), format: "woz" };
    }
  };

  insert.addEventListener("click", () => picker.click());
  picker.addEventListener("change", async () => {
    const f = picker.files?.[0];
    picker.value = "";
    if (!f) return;
    picked = true;
    if (f.size > MAX_DISK) return void say(`${f.name}: over 16 MB, too big for a 5.25-inch disk`);
    let bytes: Uint8Array;
    try {
      bytes = new Uint8Array(await f.arrayBuffer());
    } catch (err) {
      return void say(`${f.name}: ${err instanceof Error ? err.message : String(err)}`);
    }
    const disk: KeptDisk = { name: f.name, bytes, writeProtected: false, format: /\.woz$/i.test(f.name) ? "woz" : /\.po$/i.test(f.name) ? "po" : "dsk" };
    const got = await put(disk, bytes);
    if (!got) return;
    disk.format = got.format; // as the core read it (a .dsk can be in ProDOS order)
    say((await store.put(drive, disk)) ? "" : "In for now; this browser would not keep it");
    auto = ""; // the hint again, for this disk
    show(); // kept first: a reload now finds it
  });
  protect.addEventListener("click", async () => {
    if (!info || !kept) return;
    const on = !info.writeProtected;
    port.setWriteProtect(drive, on);
    info = { ...info, writeProtected: on };
    kept = { ...kept, writeProtected: on };
    await store.put(drive, kept);
    show();
  });
  save.addEventListener("click", async () => {
    if (!info || !kept) return;
    try {
      const out = await file(kept.format);
      download(savedName(info.name, out.format), out.bytes);
      say(out.format !== kept.format ? "It no longer reads as plain sectors (protection, or its own format): saved as .woz" : "");
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
    }
  });
  eject.addEventListener("click", async () => {
    port.ejectDisk(drive);
    info = kept = null;
    await store.remove(drive); // forgotten first: a reload now finds nothing
    show();
    say("");
    insert.focus(); // Eject is gone: the keyboard stays on the row
  });
  show();

  /** Once connected: the disk kept from last time goes back in, unless one was picked while the browser looked for it. */
  const restore = async () => {
    if (!port.caps.disk) return;
    const d = await store.get(drive);
    if (d && !picked && (await put(d, d.bytes))) show();
  };

  /** Keep what was written, once the drive has stopped. */
  const keepChanges = async () => {
    if (!info || !kept) return;
    ++keeping;
    try {
      const now = await port.readDiskInfo(drive);
      if (!now || now.writes === keptWrites) return;
      keptWrites = now.writes;
      const out = await file(kept.format);
      kept = { ...kept, bytes: out.bytes, format: out.format };
      await store.put(drive, kept);
    } catch {
      /* not kept: the disk in the drive is still right */
    } finally {
      --keeping;
    }
  };

  const update = (f: FrameState) => {
    const has = port.caps.disk && (f.powered ? !!f.disk : present); // off, the frame has no card to say: as it was
    if (has !== present || f.apple !== apple) {
      present = has;
      apple = f.apple;
      element.hidden = !has;
      show();
    }
    const on = !!f.disk && f.disk.spinning && f.disk.drive === drive;
    light.classList.toggle("on", on);
    pillLight.classList.toggle("on", on);
    if (spinning && !on) void keepChanges();
    spinning = on;
    if (on && info) read = true;
    const next = hint(f, on);
    if (next !== auto) {
      if (note.textContent === auto || !note.textContent) say(next); // not over a note of its own
      auto = next;
    }
  };
  /** What the note should say about booting, or "": drive 1's, which the IIe boots from. */
  const hint = (f: FrameState, on: boolean): string => {
    if (drive !== 0) return "";
    if (!info) return f.apple && on ? WAITING : ""; // the boot ROM looking for a disk that is not there
    if (read) return "";
    const needs = bootNeeds();
    if (needs) return `To boot it, the IIe needs ${needs} (under ROMs)`;
    if (!f.apple) return "Start Apple IIe boots it";
    return "Reboot boots it (or PR#6 in BASIC)";
  };
  /** Its motor runs, or what it wrote is still being kept: a restart now may lose writes. */
  const busy = () => spinning || keeping > 0;
  return { element, pill, restore, update, busy, loaded: () => !!info };
}
