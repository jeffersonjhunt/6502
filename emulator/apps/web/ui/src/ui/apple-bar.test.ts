// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { fakeIIeRom } from "../port/fake-iie";
import type { Cartridge } from "../port/machine-port";
import { appleBar } from "./apple-bar";
import { keepCartridge, keptCartridge } from "./cartridge";

const flush = () => new Promise((r) => setTimeout(r, 0));
const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;
const shown = (el: HTMLElement) => ["apple-insert", "apple-eject", "apple-start", "apple-reset", "apple-reboot"].filter((id) => !$(el, id).hidden);

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
    expect(shown(bar.element)).toEqual(["apple-insert"]);
    await pick(bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    const [[c]] = port.callsTo("insertCartridge") as [[Cartridge]];
    expect(c.rom).toEqual(fakeIIeRom());
    expect(keptCartridge()?.rom).toEqual(fakeIIeRom());
    expect(shown(bar.element)).toEqual(["apple-insert", "apple-eject", "apple-start"]);
    expect($(bar.element, "apple-insert").textContent).toBe("IIe ROM ✓");
    expect($(bar.element, "apple-note").textContent).toMatch(/No character ROM/);
  });

  test("a wrong file: says why, puts nothing in", async () => {
    const { port, bar } = make();
    await pick(bar.element, [{ name: "disk.dsk", bytes: new Uint8Array(143360) }]);
    expect(port.callsTo("insertCartridge")).toEqual([]);
    expect($(bar.element, "apple-note").textContent).toMatch(/^disk\.dsk: 143360 bytes is no IIe ROM/);
    expect(shown(bar.element)).toEqual(["apple-insert"]);
  });

  test("the machine refuses it: says why, keeps nothing", async () => {
    const { port, bar } = make();
    port.cartridgeError = "Apple IIe ROM: want 16 KB";
    await pick(bar.element, [{ name: "iie.rom", bytes: fakeIIeRom() }]);
    expect($(bar.element, "apple-note").textContent).toBe("Apple IIe ROM: want 16 KB");
    expect(keptCartridge()).toBeNull();
    expect(shown(bar.element)).toEqual(["apple-insert"]);
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
    expect(shown(bar.element)).toEqual(["apple-insert"]);
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
    expect(shown(bar.element)).toEqual(["apple-insert", "apple-eject", "apple-reset", "apple-reboot"]);
    $(bar.element, "apple-reset").click();
    expect(port.callsTo("sendKey")).toEqual([[0xe0, true], [0x4c, true], [0x4c, false], [0xe0, false]]); // Ctrl-Delete
    port.calls.length = 0;
    $(bar.element, "apple-reboot").click();
    expect(port.callsTo("sendKey")).toEqual([[0xe0, true], [0xe3, true], [0x4c, true], [0x4c, false], [0xe3, false], [0xe0, false]]);
    port.emit({ powered: false, apple: false });
    expect(shown(bar.element)).toContain("apple-start");
    expect(($(bar.element, "apple-start") as HTMLButtonElement).disabled).toBe(true); // power is off
  });

  test("a backend without a cartridge slot: no bar", async () => {
    const port = new FakePort({ cartridge: false });
    const bar = appleBar(port, { started: () => {} });
    await bar.restore();
    expect(bar.element.hidden).toBe(true);
  });
});
