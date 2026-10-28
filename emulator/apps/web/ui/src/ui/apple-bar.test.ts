// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { fakeIIeRom } from "../port/fake-iie";
import type { Cartridge } from "../port/machine-port";
import { appleBar, bootNeeds } from "./apple-bar";
import { keepCartridge, keptCartridge } from "./cartridge";

const flush = () => new Promise((r) => setTimeout(r, 0));
const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;
type Bar = ReturnType<typeof appleBar>;
const shown = (bar: Bar) =>
  ["apple-insert", "apple-eject", "apple-start", "apple-reset", "apple-reboot"].filter((id) => {
    const el = bar.element.querySelector<HTMLElement>(`[data-testid="${id}"]`) ?? $(bar.resets, id);
    return !el.hidden && !el.closest("[hidden]");
  });

async function pick(bar: HTMLElement, files: { name: string; bytes: Uint8Array }[]) {
  const input = $(bar, "apple-file") as HTMLInputElement;
  Object.defineProperty(input, "files", { configurable: true, value: files.map((f) => new File([new Uint8Array(f.bytes)], f.name)) });
  input.dispatchEvent(new Event("change"));
  for (let i = 0; i < 5; ++i) await flush();
}

function make() {
  const port = new FakePort({ usb: true });
  let started = 0;
  const bar = appleBar(port, { started: () => ++started });
  return { port, bar, started: () => started };
}

describe("the Apple bar", () => {
  beforeEach(() => localStorage.clear());

  test("empty: only Insert; a ROM picked goes in, is kept, and Start shows", async () => {
    const { port, bar } = make();
    await bar.restore();
    expect(port.callsTo("insertCartridge")).toEqual([]); // nothing kept: nothing to put back
    expect(shown(bar)).toEqual(["apple-insert"]);
    await pick(bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    const [[c]] = port.callsTo("insertCartridge") as [[Cartridge]];
    expect(c.rom).toEqual(fakeIIeRom());
    expect(keptCartridge()?.rom).toEqual(fakeIIeRom());
    expect(shown(bar)).toEqual(["apple-eject", "apple-start"]);
    expect($(bar.element, "apple-meta").textContent).toBe("16 KB · yours, kept in this browser");
    expect($(bar.element, "apple-note").textContent).toMatch(/No character ROM/);
  });

  test("a wrong file: says why, puts nothing in", async () => {
    const { port, bar } = make();
    await pick(bar.element, [{ name: "disk.dsk", bytes: new Uint8Array(143360) }]);
    expect(port.callsTo("insertCartridge")).toEqual([]);
    expect($(bar.element, "apple-note").textContent).toMatch(/^disk\.dsk: 143360 bytes is no IIe ROM/);
    expect(shown(bar)).toEqual(["apple-insert"]);
  });

  test("a file too big to be a ROM is not read; one that can't be read says so", async () => {
    const { port, bar } = make();
    const input = $(bar.element, "apple-file") as HTMLInputElement;
    const choose = async (file: object) => {
      Object.defineProperty(input, "files", { configurable: true, value: [file] });
      input.dispatchEvent(new Event("change"));
      for (let i = 0; i < 5; ++i) await flush();
    };
    let reads = 0;
    await choose({ name: "movie.mp4", size: 2 ** 31, arrayBuffer: async () => (++reads, new ArrayBuffer(0)) });
    expect(reads).toBe(0);
    expect($(bar.element, "apple-note").textContent).toMatch(/^movie\.mp4: 2147483648 bytes is no IIe ROM/);
    await choose({ name: "gone.rom", size: 16384, arrayBuffer: async () => Promise.reject(new Error("the file could not be read")) });
    expect($(bar.element, "apple-note").textContent).toBe("gone.rom: the file could not be read");
    expect(port.callsTo("insertCartridge")).toEqual([]);
  });

  test("the machine refuses it: says why, keeps nothing", async () => {
    const { port, bar } = make();
    port.cartridgeError = "Apple IIe ROM: want 16 KB";
    await pick(bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    expect($(bar.element, "apple-note").textContent).toBe("Apple IIe ROM: want 16 KB");
    expect(keptCartridge()).toBeNull();
    expect(shown(bar)).toEqual(["apple-insert"]);
  });

  test("kept from last time: back in once connected; Eject takes it out and forgets it", async () => {
    keepCartridge({ rom: fakeIIeRom(), chars: null, diskBoot: null, diskSequencer: null });
    const { port, bar } = make();
    await bar.restore();
    expect((port.callsTo("insertCartridge")[0][0] as Cartridge).rom).toEqual(fakeIIeRom());
    $(bar.element, "apple-eject").click();
    await flush();
    expect(port.callsTo("insertCartridge")[1]).toEqual([null]);
    expect(keptCartridge()).toBeNull();
    expect(shown(bar)).toEqual(["apple-insert"]);
  });

  test("Start: START, then C806R at the monitor; in Apple II mode, the IIe's resets", async () => {
    keepCartridge({ rom: fakeIIeRom(), chars: null, diskBoot: null, diskSequencer: null });
    const { port, bar, started } = make();
    await bar.restore();
    $(bar.element, "apple-start").click();
    expect(port.calls.filter((c) => c[0] === "press" || c[0] === "sendSerial")).toEqual([
      ["press", "start"],
      ["sendSerial", Array.from("C806R\r", (ch) => ch.charCodeAt(0))],
    ]);
    expect(started()).toBe(1);
    port.onFrame(bar.update);
    port.emit({ apple: true, fixedHz: 1_020_484 });
    expect(shown(bar)).toEqual(["apple-eject", "apple-reset", "apple-reboot"]);
    $(bar.resets, "apple-reset").click();
    expect(port.callsTo("sendKey")).toEqual([[0xe0, true], [0x4c, true], [0x4c, false], [0xe0, false]]); // Ctrl-Delete
    port.calls.length = 0;
    $(bar.resets, "apple-reboot").click();
    expect(port.callsTo("sendKey")).toEqual([[0xe0, true], [0xe3, true], [0x4c, true], [0x4c, false], [0xe3, false], [0xe0, false]]);
    port.emit({ powered: false, apple: false });
    expect(shown(bar)).toContain("apple-start");
    expect(($(bar.element, "apple-start") as HTMLButtonElement).disabled).toBe(true); // power is off
  });

  test("each part has its own row: loaded and ejected alone, and only with the IIe ROM in", async () => {
    const { port, bar } = make();
    const boot = new Uint8Array(256);
    boot.set([0xa2, 0x20, 0xa0, 0x00, 0xa2, 0x03, 0x86, 0x3c]);
    expect(($(bar.element, "apple-boot-load") as HTMLButtonElement).disabled).toBe(true); // nothing to join yet
    await pick(bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    expect(($(bar.element, "apple-boot-load") as HTMLButtonElement).disabled).toBe(false);
    const input = $(bar.element, "apple-boot-file") as HTMLInputElement;
    Object.defineProperty(input, "files", { configurable: true, value: [new File([boot], "p5.bin")] });
    input.dispatchEvent(new Event("change"));
    for (let i = 0; i < 5; ++i) await flush();
    const c = port.callsTo("insertCartridge").at(-1)![0] as Cartridge;
    expect(c.diskBoot).toEqual(boot);
    expect(c.rom).toEqual(fakeIIeRom());
    expect($(bar.element, "apple-boot-meta").textContent).toBe("256 bytes · yours, kept in this browser");
    expect(keptCartridge()?.diskBoot).toEqual(boot);
    expect($(bar.element, "apple-note").textContent).toMatch(/No Disk II sequencer/);
    $(bar.element, "apple-boot-eject").click();
    await flush();
    expect((port.callsTo("insertCartridge").at(-1)![0] as Cartridge).diskBoot).toBeNull();
    expect(keptCartridge()?.rom).toEqual(fakeIIeRom()); // the rest stays
    expect($(bar.element, "apple-boot-meta").textContent).toBe("none: the IIe won't boot from the drive");
  });

  test("a part in or out restarts the machine: the buttons and the note say so", async () => {
    const { bar } = make();
    expect($(bar.element, "apple-insert").title).toMatch(/Restarts the machine$/);
    await pick(bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    expect($(bar.element, "apple-note").textContent).toMatch(/^The machine restarted\. No character ROM/);
    expect($(bar.element, "apple-eject").title).toMatch(/restarts the machine$/);
    expect($(bar.element, "apple-chars-load").title).toMatch(/Restarts the machine$/);
    expect($(bar.element, "apple-chars-eject").title).toMatch(/restarts the machine$/);
  });

  test("in Apple II mode a part change asks first, and No leaves the IIe running", async () => {
    keepCartridge({ rom: fakeIIeRom(), chars: new Uint8Array(4096), diskBoot: null, diskSequencer: null });
    const asked: string[] = [];
    const answer = { yes: false };
    const port = new FakePort({ usb: true });
    const bar = appleBar(port, { started: () => {}, confirm: (q) => (asked.push(q), answer.yes) });
    await bar.restore();
    port.onFrame(bar.update);
    port.emit({ apple: true });
    $(bar.element, "apple-chars-eject").click();
    await flush();
    expect(asked).toEqual(["Eject the IIe character ROM? That restarts the machine: the IIe goes back to the BIOS, and what it runs now is lost."]);
    expect(port.callsTo("insertCartridge").length).toBe(1); // the restore's alone
    expect(keptCartridge()?.chars).not.toBeNull();
    answer.yes = true;
    $(bar.element, "apple-chars-eject").click();
    await flush();
    expect(port.callsTo("insertCartridge").length).toBe(2);
    port.emit({ apple: false });
    $(bar.element, "apple-eject").click(); // at the BIOS: nothing to lose, no question
    await flush();
    expect(asked.length).toBe(2);
  });

  test("ejecting the IIe ROM keeps the other parts: they go back in with the next one", async () => {
    const chars = new Uint8Array(4096).fill(7);
    keepCartridge({ rom: fakeIIeRom(), chars, diskBoot: null, diskSequencer: null });
    const { port, bar } = make();
    await bar.restore();
    $(bar.element, "apple-eject").click();
    await flush();
    expect(port.callsTo("insertCartridge").at(-1)).toEqual([null]);
    expect(keptCartridge()).toBeNull();
    expect($(bar.element, "apple-chars-meta").textContent).toBe("4 KB · yours, kept for the next IIe ROM");
    expect($(bar.element, "apple-note").textContent).toBe("The machine restarted. The other parts are kept for the next IIe ROM");
    const again = make(); // next time too
    await again.bar.restore();
    await pick(again.bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    expect((again.port.callsTo("insertCartridge").at(-1)![0] as Cartridge).chars).toEqual(chars);
    expect(keptCartridge()?.chars).toEqual(chars);
  });

  test("a part kept with no IIe ROM in: Eject just forgets it", async () => {
    keepCartridge({ rom: fakeIIeRom(), chars: new Uint8Array(4096), diskBoot: null, diskSequencer: null });
    localStorage.removeItem("oo1.apple-rom");
    const { port, bar } = make();
    await bar.restore();
    $(bar.element, "apple-chars-eject").click();
    await flush();
    expect(port.callsTo("insertCartridge")).toEqual([]);
    expect($(bar.element, "apple-chars-meta").textContent).toBe("none: the built-in font");
    expect(localStorage.getItem("oo1.apple-chars")).toBeNull();
  });

  test("the keyboard follows the row: Load… to Eject once in, Eject to Load… once out", async () => {
    const { bar } = make();
    document.body.append(bar.element);
    $(bar.element, "apple-insert").focus();
    await pick(bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    expect(document.activeElement).toBe($(bar.element, "apple-eject"));
    const input = $(bar.element, "apple-chars-file") as HTMLInputElement;
    Object.defineProperty(input, "files", { configurable: true, value: [new File([new Uint8Array(4096)], "video.bin")] });
    $(bar.element, "apple-chars-load").focus();
    input.dispatchEvent(new Event("change"));
    for (let i = 0; i < 5; ++i) await flush();
    expect(document.activeElement).toBe($(bar.element, "apple-chars-eject"));
    $(bar.element, "apple-chars-eject").click();
    for (let i = 0; i < 5; ++i) await flush();
    expect(document.activeElement).toBe($(bar.element, "apple-chars-load"));
    $(bar.element, "apple-eject").focus();
    $(bar.element, "apple-eject").click();
    for (let i = 0; i < 5; ++i) await flush();
    expect(document.activeElement).toBe($(bar.element, "apple-insert"));
    bar.element.remove();
  });

  test("a backend without a cartridge slot: no bar", async () => {
    const port = new FakePort({ cartridge: false });
    const bar = appleBar(port, { started: () => {} });
    await bar.restore();
    expect(bar.element.hidden).toBe(true);
  });
});

test("what booting from the drive still needs", () => {
  const rom = new Uint8Array(16384), prom = new Uint8Array(256);
  const c = (diskBoot: Uint8Array | null, diskSequencer: Uint8Array | null): Cartridge => ({ rom, chars: null, diskBoot, diskSequencer });
  expect(bootNeeds(null)).toBe("the IIe ROM and the Disk II PROMs");
  expect(bootNeeds(c(null, null))).toBe("the Disk II PROMs");
  expect(bootNeeds(c(null, prom))).toBe("the Disk II boot ROM (341-0027)");
  expect(bootNeeds(c(prom, null))).toBe("the Disk II sequencer (341-0028)");
  expect(bootNeeds(c(prom, prom))).toBe("");
});
