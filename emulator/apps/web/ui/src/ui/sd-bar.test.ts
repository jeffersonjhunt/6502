// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import type { FrameState, SdStatus } from "../port/machine-port";
import { cardSize, sdBar } from "./sd-bar";

const flush = async () => {
  for (let i = 0; i < 6; ++i) await new Promise((r) => setTimeout(r, 0));
};
const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;
const shown = (el: HTMLElement) => ["sd-insert", "sd-protect", "sd-save", "sd-eject"].filter((id) => !$(el, id).hidden);
const frame = (sd: Partial<SdStatus> | null): Partial<FrameState> => ({ sd: sd ? { present: true, busy: false, changed: false, ...sd } : null });

async function pick(bar: HTMLElement, name: string, bytes: Uint8Array) {
  const input = $(bar, "sd-file") as HTMLInputElement;
  Object.defineProperty(input, "files", { configurable: true, value: [new File([new Uint8Array(bytes)], name)] });
  input.dispatchEvent(new Event("change"));
  await flush();
}

function make() {
  const port = new FakePort();
  const downloads: [string, Uint8Array][] = [];
  const listeners: ((e: Event) => void)[] = [];
  const win = { addEventListener: (_: string, l: (e: Event) => void) => listeners.push(l) } as unknown as Window;
  const bar = sdBar(port, (name, bytes) => downloads.push([name, bytes]), win);
  port.onFrame(bar.update);
  /** What leaving the page would do: true if it asks first. */
  const leaving = () => {
    const e = new Event("beforeunload", { cancelable: true });
    for (const l of listeners) l(e);
    return e.defaultPrevented;
  };
  return { port, bar, downloads, leaving };
}

describe("the SD slot", () => {
  test("sizes read as a card's", () => {
    expect(cardSize(2880)).toBe("1440 KB");
    expect(cardSize(65536)).toBe("32 MB");
    expect(cardSize(81920)).toBe("40 MB");
    expect(cardSize(1)).toBe("0.5 KB");
    expect(cardSize(30000)).toBe("15 MB");
  });

  test("shown only on a machine with the card; the light is a command in the frame", () => {
    const { port, bar } = make();
    port.emit(frame(null));
    expect(bar.element.hidden).toBe(true);
    port.emit(frame({ present: false }));
    expect(bar.element.hidden).toBe(false);
    expect($(bar.element, "sd-light").classList.contains("on")).toBe(false);
    port.emit(frame({ busy: true }));
    expect($(bar.element, "sd-light").classList.contains("on")).toBe(true);
    port.emit(frame({}));
    expect($(bar.element, "sd-light").classList.contains("on")).toBe(false);
  });

  test("a card picked goes in, says how to use it, and Eject takes it out", async () => {
    const { port, bar } = make();
    expect(shown(bar.element)).toEqual(["sd-insert"]);
    await pick(bar.element, "card.img", new Uint8Array(2880 * 512));
    expect(port.callsTo("insertSd").map(([b, n]) => [(b as Uint8Array).length, n])).toEqual([[2880 * 512, "card.img"]]);
    expect($(bar.element, "sd-insert").textContent).toBe("card.img (1440 KB)");
    expect($(bar.element, "sd-note").textContent).toContain("C809R");
    expect(shown(bar.element)).toEqual(["sd-insert", "sd-protect", "sd-save", "sd-eject"]);
    $(bar.element, "sd-eject").click();
    expect(port.callsTo("ejectSd")).toEqual([[]]);
    expect(shown(bar.element)).toEqual(["sd-insert"]);
    expect($(bar.element, "sd-insert").textContent).toBe("Insert SD card…");
  });

  test("a card the port refuses says why", async () => {
    const { port, bar } = make();
    port.sdError = "card.img: not an SD card image (less than one 512-byte sector)";
    await pick(bar.element, "card.img", new Uint8Array(10));
    expect($(bar.element, "sd-note").textContent).toBe(port.sdError);
    expect(shown(bar.element)).toEqual(["sd-insert"]);
  });

  test("write-protect is the card's switch", async () => {
    const { port, bar } = make();
    await pick(bar.element, "card.img", new Uint8Array(512));
    $(bar.element, "sd-protect").click();
    expect(port.callsTo("setSdWriteProtect")).toEqual([[true]]);
    expect($(bar.element, "sd-protect").getAttribute("aria-pressed")).toBe("true");
    $(bar.element, "sd-protect").click();
    expect(port.callsTo("setSdWriteProtect")).toEqual([[true], [false]]);
    expect($(bar.element, "sd-protect").getAttribute("aria-pressed")).toBe("false");
  });

  test("writes are noted, held against leaving, and saved by Save card", async () => {
    const { port, bar, downloads, leaving } = make();
    await pick(bar.element, "card.img", new Uint8Array(512));
    expect(leaving()).toBe(false);
    port.emit(frame({ changed: true }));
    expect($(bar.element, "sd-note").textContent).toContain("Save card");
    expect($(bar.element, "sd-save").classList.contains("on")).toBe(true);
    expect(leaving()).toBe(true);
    $(bar.element, "sd-save").click();
    await flush();
    expect(downloads).toEqual([["card.img", port.sdSaved]]);
    expect($(bar.element, "sd-save").classList.contains("on")).toBe(false);
    expect(leaving()).toBe(false);
    port.emit(frame({ changed: false })); // the port agrees: nothing to say
    expect($(bar.element, "sd-note").textContent).toBe("");
  });

  test("with no card, frames change nothing and leaving doesn't ask", () => {
    const { port, bar, leaving } = make();
    port.emit(frame({ present: false, changed: true }));
    expect(shown(bar.element)).toEqual(["sd-insert"]);
    expect(leaving()).toBe(false);
  });
});
