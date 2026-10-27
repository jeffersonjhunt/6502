// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import type { DiskStatus, FrameState } from "../port/machine-port";
import { diskBar, savedName } from "./disk-bar";
import { memoryDiskStore } from "./disk-store";

const flush = async () => {
  for (let i = 0; i < 6; ++i) await new Promise((r) => setTimeout(r, 0));
};
const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;
const shown = (el: HTMLElement) => ["disk-insert", "disk-protect", "disk-save", "disk-eject"].filter((id) => !$(el, id).hidden);
const STATUS: DiskStatus = { motor: false, spinning: false, drive: 0, q6: false, q7: false, data: 0, phases: 0, quarterTracks: [0, 0] };
const frame = (disk: Partial<DiskStatus> | null): Partial<FrameState> => ({ disk: disk ? { ...STATUS, ...disk } : null });

async function pick(bar: HTMLElement, name: string, bytes: Uint8Array) {
  const input = $(bar, "disk-file") as HTMLInputElement;
  Object.defineProperty(input, "files", { configurable: true, value: [new File([new Uint8Array(bytes)], name)] });
  input.dispatchEvent(new Event("change"));
  await flush();
}

async function pick2(bar: HTMLElement, name: string, bytes: Uint8Array) {
  const input = $(bar, "disk-file-2") as HTMLInputElement;
  Object.defineProperty(input, "files", { configurable: true, value: [new File([new Uint8Array(bytes)], name)] });
  input.dispatchEvent(new Event("change"));
  await flush();
}

function make() {
  const port = new FakePort();
  const store = memoryDiskStore();
  const downloads: [string, Uint8Array][] = [];
  const bar = diskBar(port, store, (name, bytes) => downloads.push([name, bytes]));
  port.onFrame(bar.update);
  return { port, store, bar, downloads };
}

describe("the drive", () => {
  test("shown only on a machine with the card; the light is the drive's motor", () => {
    const { port, bar } = make();
    port.emit(frame(null));
    expect(bar.element.hidden).toBe(true);
    port.emit(frame({}));
    expect(bar.element.hidden).toBe(false);
    expect($(bar.element, "disk-light").classList.contains("on")).toBe(false);
    port.emit(frame({ spinning: true }));
    expect($(bar.element, "disk-light").classList.contains("on")).toBe(true);
    port.emit(frame({ spinning: true, drive: 1 })); // drive 2's motor: not this light
    expect($(bar.element, "disk-light").classList.contains("on")).toBe(false);
  });

  test("a disk picked goes in and is kept; Eject takes it out and forgets it", async () => {
    const { port, store, bar } = make();
    expect(shown(bar.element)).toEqual(["disk-insert"]);
    await pick(bar.element, "Trail.woz", new Uint8Array([9, 9]));
    expect(port.callsTo("insertDisk")).toEqual([[0, new Uint8Array([9, 9]), "Trail.woz"]]);
    expect($(bar.element, "disk-name").textContent).toBe("Drive 1 · Trail.woz");
    expect(shown(bar.element)).toEqual(["disk-protect", "disk-save", "disk-eject"]);
    expect((await store.get(0))?.name).toBe("Trail.woz");
    $(bar.element, "disk-eject").click();
    await flush();
    expect(port.callsTo("ejectDisk")).toEqual([[0]]);
    expect(await store.get(0)).toBeNull();
    expect(shown(bar.element)).toEqual(["disk-insert"]);
  });

  test("a file that is no disk: says why, and nothing is kept", async () => {
    const { port, store, bar } = make();
    port.diskError = "notes.txt: 5 bytes is no disk image";
    await pick(bar.element, "notes.txt", new Uint8Array(5));
    expect($(bar.element, "disk-note").textContent).toBe("notes.txt: 5 bytes is no disk image");
    expect(await store.get(0)).toBeNull();
    expect(shown(bar.element)).toEqual(["disk-insert"]);
  });

  test("a file too big to be a disk is not read; one that can't be read says so", async () => {
    const { port, store, bar } = make();
    const input = $(bar.element, "disk-file") as HTMLInputElement;
    const choose = async (file: object) => {
      Object.defineProperty(input, "files", { configurable: true, value: [file] });
      input.dispatchEvent(new Event("change"));
      await flush();
    };
    let reads = 0;
    await choose({ name: "movie.dsk", size: 2 ** 31, arrayBuffer: async () => (++reads, new ArrayBuffer(0)) });
    expect(reads).toBe(0);
    expect($(bar.element, "disk-note").textContent).toBe("movie.dsk: over 16 MB, too big for a 5.25-inch disk");
    await choose({ name: "gone.woz", size: 1000, arrayBuffer: async () => Promise.reject(new Error("the file could not be read")) });
    expect($(bar.element, "disk-note").textContent).toBe("gone.woz: the file could not be read");
    expect(port.callsTo("insertDisk")).toEqual([]);
    expect(await store.get(0)).toBeNull();
  });

  test("write-protect is the disk's, and kept with it", async () => {
    const { port, store, bar } = make();
    await pick(bar.element, "a.dsk", new Uint8Array(4));
    $(bar.element, "disk-protect").click();
    await flush();
    expect(port.callsTo("setWriteProtect")).toEqual([[0, true]]);
    expect($(bar.element, "disk-protect").getAttribute("aria-pressed")).toBe("true");
    expect((await store.get(0))?.writeProtected).toBe(true);
  });

  test("kept from last time: back in once connected, write-protect and all", async () => {
    const { port, store, bar } = make();
    await store.put(0, { name: "old.po", bytes: new Uint8Array([1]), writeProtected: true, format: "po" });
    await bar.restore();
    expect(port.callsTo("insertDisk")).toEqual([[0, new Uint8Array([1]), "old.po"]]);
    expect(port.callsTo("setWriteProtect")).toEqual([[0, true]]);
    expect($(bar.element, "disk-name").textContent).toBe("Drive 1 · old.po");
  });

  test("a disk picked while the browser still looks for the kept one stays in", async () => {
    const port = new FakePort();
    const store = memoryDiskStore();
    await store.put(0, { name: "old.po", bytes: new Uint8Array([1]), writeProtected: false, format: "po" });
    let answer = () => {};
    const slow = { ...store, get: (d: number) => new Promise<Awaited<ReturnType<typeof store.get>>>((r) => (answer = () => void store.get(d).then(r))) };
    const bar = diskBar(port, slow, () => {});
    const restoring = bar.restore();
    await pick(bar.element, "new.dsk", new Uint8Array([2]));
    answer();
    await restoring;
    expect(port.callsTo("insertDisk").map((c) => c[2])).toEqual(["new.dsk"]);
    expect($(bar.element, "disk-name").textContent).toBe("Drive 1 · new.dsk");
  });

  test("a slow pick (the file still being read) and a restore: only the pick goes in, and it is what is kept", async () => {
    const { port, store, bar } = make();
    await store.put(0, { name: "old.po", bytes: new Uint8Array([1]), writeProtected: false, format: "po" });
    let read = () => {};
    const input = $(bar.element, "disk-file") as HTMLInputElement;
    const file = { name: "new.woz", size: 1, arrayBuffer: () => new Promise<ArrayBuffer>((r) => (read = () => r(new Uint8Array([2]).buffer))) };
    Object.defineProperty(input, "files", { configurable: true, value: [file] });
    input.dispatchEvent(new Event("change"));
    await bar.restore(); // finds old.po while new.woz is still being read
    read();
    await flush();
    expect(port.callsTo("insertDisk").map((c) => c[2])).toEqual(["new.woz"]);
    expect(await store.get(0)).toMatchObject({ name: "new.woz", format: "woz" });
  });

  test("Save downloads it in its own format, or as .woz when it no longer fits one", async () => {
    const { port, bar, downloads } = make();
    await pick(bar.element, "Master.dsk", new Uint8Array(4));
    $(bar.element, "disk-save").click();
    await flush();
    expect(port.callsTo("saveDisk")).toEqual([[0, "dsk"]]);
    expect(downloads.map((d) => d[0])).toEqual(["Master.dsk"]);
    let calls = 0;
    port.saveDisk = async (_drive, format) => {
      if (++calls === 1 || format !== "woz") throw new Error("track 3, sector 0 does not read as a plain sector");
      return new Uint8Array([7]);
    };
    $(bar.element, "disk-save").click();
    await flush();
    expect(downloads.map((d) => d[0])).toEqual(["Master.dsk", "Master.woz"]);
    expect($(bar.element, "disk-note").textContent).toMatch(/saved as \.woz/);
  });

  test("what is written is kept once the drive stops, and only then", async () => {
    const { port, store, bar } = make();
    port.onFrame(bar.update);
    port.emit(frame({}));
    await pick(bar.element, "Master.dsk", new Uint8Array(4));
    port.saved = new Uint8Array([5, 5]);
    port.emit(frame({ spinning: true }));
    port.disks[0]!.writes = 40; // DOS wrote
    await flush();
    expect((await store.get(0))?.bytes).toEqual(new Uint8Array(4)); // not while it spins
    port.emit(frame({ spinning: false }));
    await flush();
    expect((await store.get(0))?.bytes).toEqual(new Uint8Array([5, 5]));
    expect(port.callsTo("saveDisk")).toEqual([[0, "dsk"]]);
    port.emit(frame({ spinning: true }));
    port.emit(frame({ spinning: false })); // read only, this time
    await flush();
    expect(port.callsTo("saveDisk").length).toBe(1);
  });

  test("busy while the motor runs, and until what it wrote is kept", async () => {
    const { port, bar } = make();
    port.onFrame(bar.update);
    port.emit(frame({}));
    await pick(bar.element, "Master.dsk", new Uint8Array(4));
    expect(bar.busy()).toBe(false);
    port.emit(frame({ spinning: true }));
    expect(bar.busy()).toBe(true);
    port.disks[0]!.writes = 40;
    port.emit(frame({ spinning: false }));
    expect(bar.busy()).toBe(true); // keeping it
    await flush();
    expect(bar.busy()).toBe(false);
  });

  test("the IIe looking for a disk with none in: says so, and why it seems stuck", async () => {
    const { port, bar } = make();
    port.emit({ apple: true, ...frame({ spinning: true }) });
    expect($(bar.element, "disk-note").textContent).toMatch(/^No disk: the IIe waits for one/);
    port.emit({ apple: true, ...frame({ spinning: false }) }); // Ctrl-Reset: BASIC
    expect($(bar.element, "disk-note").textContent).toBe("");
    await pick(bar.element, "a.dsk", new Uint8Array(4));
    port.emit({ apple: true, ...frame({ spinning: true }) }); // a disk in: just reading
    expect($(bar.element, "disk-note").textContent).toBe("");
  });

  test("a disk in boots nothing by itself: the note says what will, until the drive reads it", async () => {
    let needs = "the Disk II PROMs";
    const port = new FakePort();
    const bar = diskBar(port, memoryDiskStore(), () => {}, { bootNeeds: () => needs });
    port.onFrame(bar.update);
    const note = () => $(bar.element, "disk-note").textContent;
    port.emit(frame({}));
    expect(note()).toBe(""); // no disk: nothing to boot
    await pick(bar.element, "Trail.woz", new Uint8Array(4));
    port.emit(frame({}));
    expect(note()).toBe("To boot it, the IIe needs the Disk II PROMs (under ROMs)");
    await pick(bar.element, "Trail.dsk", new Uint8Array(4)); // another, unread: the same hint
    port.emit(frame({}));
    expect(note()).toBe("To boot it, the IIe needs the Disk II PROMs (under ROMs)");
    needs = "";
    port.emit(frame({}));
    expect(note()).toBe("Start Apple IIe boots it");
    port.emit({ apple: true, ...frame({}) }); // at BASIC's prompt, say
    expect(note()).toBe("Reboot boots it (or PR#6 in BASIC)");
    port.emit({ apple: true, ...frame({ spinning: true }) });
    expect(note()).toBe("");
    port.emit({ apple: true, ...frame({ spinning: false }) }); // the program done with the drive
    expect(note()).toBe("");
    await pick(bar.element, "Side B.woz", new Uint8Array(4)); // another disk: not read yet
    port.emit({ apple: true, ...frame({}) });
    expect(note()).toBe("Reboot boots it (or PR#6 in BASIC)");
  });

  test("the hint does not cover a note of the bar's own", async () => {
    const port = new FakePort();
    let needs = "the Disk II PROMs";
    const bar = diskBar(port, memoryDiskStore(), () => {}, { bootNeeds: () => needs });
    port.onFrame(bar.update);
    await pick(bar.element, "Trail.woz", new Uint8Array(4));
    port.emit(frame({}));
    expect($(bar.element, "disk-note").textContent).toMatch(/^To boot it/);
    port.diskError = "x.txt: 4 bytes is no disk image";
    await pick(bar.element, "x.txt", new Uint8Array(4)); // Trail.woz stays in
    port.emit(frame({}));
    needs = ""; // the PROMs in: another hint, but the reason stays
    port.emit(frame({}));
    expect($(bar.element, "disk-note").textContent).toBe("x.txt: 4 bytes is no disk image");
  });

  test("its size and format, and its pill: a disk in, or in Apple II mode an empty drive too; the pill opens Settings", async () => {
    const port = new FakePort();
    let opened = 0;
    const bar = diskBar(port, memoryDiskStore(), () => {}, { open: () => ++opened });
    port.onFrame(bar.update);
    port.emit(frame({}));
    expect(bar.pill.hidden).toBe(true); // empty, not in Apple II mode
    port.emit({ apple: true, ...frame({}) });
    expect(bar.pill.hidden).toBe(false);
    expect(bar.pill.textContent).toBe("D1 empty");
    port.emit(frame({}));
    await pick(bar.element, "Trail.dsk", new Uint8Array(143_360));
    expect($(bar.element, "disk-meta").textContent).toBe("140 KB · DOS order");
    expect(bar.pill.hidden).toBe(false);
    expect(bar.pill.textContent).toBe("D1 Trail.dsk");
    port.emit(frame({ spinning: true }));
    expect(bar.pill.querySelector(".led")!.classList.contains("on")).toBe(true);
    bar.pill.click();
    expect(opened).toBe(1);
    port.emit(frame(null)); // no card: no pill
    expect(bar.pill.hidden).toBe(true);
  });

  test("power off: the row and the pill stay, the light out, and a disk still goes in", async () => {
    const { port, bar } = make();
    port.onFrame(bar.update);
    port.emit(frame({}));
    await pick(bar.element, "Trail.dsk", new Uint8Array(4));
    port.emit(frame({ spinning: true }));
    port.emit({ powered: false, disk: null }); // the port has no card to report while off
    expect(bar.element.hidden).toBe(false);
    expect(bar.pill.hidden).toBe(false);
    expect(bar.pill.querySelector(".led")!.classList.contains("on")).toBe(false);
    $(bar.element, "disk-eject").click();
    await flush();
    expect(shown(bar.element)).toEqual(["disk-insert"]);
    await pick(bar.element, "Side B.dsk", new Uint8Array(4));
    expect(bar.pill.textContent).toBe("D1 Side B.dsk");
  });

  test("drive 2: its own disk, light and testids; the boot hint is drive 1's alone", async () => {
    const port = new FakePort();
    const store = memoryDiskStore();
    const bar = diskBar(port, store, () => {}, { bootNeeds: () => "the Disk II PROMs" }, 1);
    port.onFrame(bar.update);
    port.emit(frame({}));
    await pick2(bar.element, "Side B.woz", new Uint8Array(4));
    expect(port.callsTo("insertDisk")).toEqual([[1, new Uint8Array(4), "Side B.woz"]]);
    expect((await store.get(1))?.name).toBe("Side B.woz");
    expect(await store.get(0)).toBeNull();
    expect($(bar.element, "disk-name-2").textContent).toBe("Drive 2 · Side B.woz");
    expect(bar.pill.textContent).toBe("D2 Side B.woz");
    port.emit(frame({ spinning: true, drive: 0 }));
    expect($(bar.element, "disk-light-2").classList.contains("on")).toBe(false);
    port.emit(frame({ spinning: true, drive: 1 }));
    expect($(bar.element, "disk-light-2").classList.contains("on")).toBe(true);
    expect($(bar.element, "disk-note-2").textContent).toBe("");
    $(bar.element, "disk-eject-2").click();
    await flush();
    expect(port.callsTo("ejectDisk")).toEqual([[1]]);
  });

  test("names: the stem, the format's extension", () => {
    expect(savedName("The Oregon Trail.dsk", "woz")).toBe("The Oregon Trail.woz");
    expect(savedName("X.PO", "po")).toBe("X.po");
    expect(savedName("image", "dsk")).toBe("image.dsk");
  });
});
