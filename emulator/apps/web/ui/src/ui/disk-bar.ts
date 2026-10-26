/**
 * The Disk II's drive 1 under the monitor: Insert disk… (.woz, .dsk, .do,
 * .po), the drive's light (on while its motor runs), write-protect, Save
 * and Eject. Shown when the machine has the card.
 *
 * The disk is kept in this browser (ui/disk-store.ts) as it went in, and
 * again whenever the motor stops after it was written to: in the format it
 * came in while it still fits, else as WOZ. Save downloads it the same way,
 * saying so when a sector image had to become a .woz.
 *
 * A disk going in boots nothing by itself, as on a real IIe, so until the
 * drive first spins with it the note says what will: the PROMs, if
 * bootNeeds() says they are missing; Start Apple IIe; or Reboot.
 */

import type { DiskFormat, DiskInfo, FrameState, MachinePort } from "../port/machine-port";
import type { DiskStore, KeptDisk } from "./disk-store";
import { h } from "./dom";

const DRIVE = 0;
const ACCEPT = ".woz,.dsk,.do,.po";
/** A 5.25-inch disk's image is a few hundred KB, a few MB as a WOZ with flux tracks: bigger is a wrong file. */
const MAX_DISK = 16 * 1024 * 1024;

/** The name a disk is saved under in a format: the same stem, that format's extension. */
export function savedName(name: string, format: DiskFormat): string {
  const stem = name.replace(/\.(woz|dsk|do|po)$/i, "") || "disk";
  return `${stem}.${format}`;
}

export function diskBar(
  port: MachinePort,
  store: DiskStore,
  download: (name: string, bytes: Uint8Array) => void,
  bootNeeds: () => string = () => "",
) {
  const picker = h("input", { type: "file", accept: ACCEPT, hidden: true, "data-testid": "disk-file" });
  const light = h("span", { class: "disk-led", title: "The drive's light: on while its motor runs", "data-testid": "disk-light" });
  const insert = h("button", { class: "tbtn", type: "button", title: "A .woz, .dsk, .do or .po of your own: it stays in this browser", "data-testid": "disk-insert" }, "Insert disk…");
  const protect = h("button", { class: "tbtn", type: "button", "aria-pressed": "false", title: "The disk's write-protect notch", "data-testid": "disk-protect" }, "Write-protect");
  const save = h("button", { class: "tbtn", type: "button", title: "Download the disk as it is now", "data-testid": "disk-save" }, "Save");
  const eject = h("button", { class: "tbtn", type: "button", title: "Take the disk out, and forget it in this browser", "data-testid": "disk-eject" }, "Eject");
  const note = h("span", { class: "apple-note", role: "status", "data-testid": "disk-note" });
  const element = h("div", { class: "apple-bar disk-bar", hidden: true, "data-testid": "disk-bar" }, picker, light, insert, protect, save, eject, note);

  let info: DiskInfo | null = null;
  let kept: KeptDisk | null = null;
  let keptWrites = 0;
  let spinning = false;
  let read = false; // the drive has spun with this disk in
  let auto = ""; // the note update() last put up
  const WAITING = "No disk: the IIe waits for one, as a real one does. Ctrl-Reset for BASIC.";
  const say = (text: string) => (note.textContent = text);
  const show = () => {
    insert.textContent = info ? info.name : "Insert disk…";
    insert.title = info ? `${info.name} is in drive 1. Click to put in another.` : "A .woz, .dsk, .do or .po of your own: it stays in this browser";
    protect.hidden = save.hidden = eject.hidden = !info;
    protect.setAttribute("aria-pressed", String(!!info?.writeProtected));
    protect.classList.toggle("on", !!info?.writeProtected);
  };

  const put = async (disk: KeptDisk, bytes: Uint8Array): Promise<boolean> => {
    try {
      info = await port.insertDisk(DRIVE, bytes, disk.name);
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
      return false;
    }
    if (disk.writeProtected) port.setWriteProtect(DRIVE, true);
    info = { ...info, writeProtected: disk.writeProtected };
    kept = disk;
    keptWrites = info.writes;
    read = false;
    return true;
  };

  /** The disk as a file: its own format if it still fits, else WOZ. */
  const file = async (format: DiskFormat): Promise<{ bytes: Uint8Array; format: DiskFormat }> => {
    try {
      return { bytes: await port.saveDisk(DRIVE, format), format };
    } catch (err) {
      if (format === "woz") throw err;
      return { bytes: await port.saveDisk(DRIVE, "woz"), format: "woz" };
    }
  };

  insert.addEventListener("click", () => picker.click());
  picker.addEventListener("change", async () => {
    const f = picker.files?.[0];
    picker.value = "";
    if (!f) return;
    if (f.size > MAX_DISK) return void say(`${f.name}: over 16 MB, too big for a 5.25-inch disk`);
    let bytes: Uint8Array;
    try {
      bytes = new Uint8Array(await f.arrayBuffer());
    } catch (err) {
      return void say(`${f.name}: ${err instanceof Error ? err.message : String(err)}`);
    }
    const disk: KeptDisk = { name: f.name, bytes, writeProtected: false, format: /\.woz$/i.test(f.name) ? "woz" : /\.po$/i.test(f.name) ? "po" : "dsk" };
    if (!(await put(disk, bytes))) return;
    disk.format = info!.format; // as the core read it (a .dsk can be in ProDOS order)
    say((await store.put(DRIVE, disk)) ? "" : "In for now; this browser would not keep it");
    auto = ""; // the hint again, for this disk
    show(); // kept first: a reload now finds it
  });
  protect.addEventListener("click", async () => {
    if (!info || !kept) return;
    const on = !info.writeProtected;
    port.setWriteProtect(DRIVE, on);
    info = { ...info, writeProtected: on };
    kept = { ...kept, writeProtected: on };
    await store.put(DRIVE, kept);
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
    port.ejectDisk(DRIVE);
    info = kept = null;
    await store.remove(DRIVE); // forgotten first: a reload now finds nothing
    show();
    say("");
  });
  show();

  /** Once connected: the disk kept from last time goes back in. */
  const restore = async () => {
    if (!port.caps.disk) return;
    const d = await store.get(DRIVE);
    if (d && (await put(d, d.bytes))) show();
  };

  /** Keep what was written, once the drive has stopped. */
  const keepChanges = async () => {
    if (!info || !kept) return;
    const now = await port.readDiskInfo(DRIVE);
    if (!now || now.writes === keptWrites) return;
    keptWrites = now.writes;
    try {
      const out = await file(kept.format);
      kept = { ...kept, bytes: out.bytes, format: out.format };
      await store.put(DRIVE, kept);
    } catch {
      /* not kept: the disk in the drive is still right */
    }
  };

  const update = (f: FrameState) => {
    element.hidden = !port.caps.disk || !f.disk;
    const on = !!f.disk && f.disk.spinning && f.disk.drive === DRIVE;
    light.classList.toggle("on", on);
    if (spinning && !on) void keepChanges();
    spinning = on;
    if (on && info) read = true;
    const next = hint(f, on);
    if (next !== auto) {
      if (note.textContent === auto || !note.textContent) say(next); // not over a note of its own
      auto = next;
    }
  };
  /** What the note should say about booting, or "". */
  const hint = (f: FrameState, on: boolean): string => {
    if (!info) return f.apple && on ? WAITING : ""; // the boot ROM looking for a disk that is not there
    if (read) return "";
    const needs = bootNeeds();
    if (needs) return `To boot it, the IIe needs ${needs} (Insert IIe ROM…)`;
    if (!f.apple) return "Start Apple IIe boots it";
    return "Reboot boots it (or PR#6 in BASIC)";
  };
  return { element, restore, update };
}
