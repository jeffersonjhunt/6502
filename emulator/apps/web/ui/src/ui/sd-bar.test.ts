// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import type { FrameState, SdStatus } from "../port/machine-port";
import { gzipSync } from "node:zlib";
import { cardSize, DEMO_NAME, fetchDemoCard, MAX_CARD, sdBar } from "./sd-bar";

const flush = async () => {
  for (let i = 0; i < 6; ++i) await new Promise((r) => setTimeout(r, 0));
};
const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;
const shown = (el: HTMLElement) => ["sd-insert", "sd-protect", "sd-save", "sd-eject"].filter((id) => !$(el, id).hidden);
const frame = (sd: Partial<SdStatus> | null): Partial<FrameState> => ({ sd: sd ? { present: true, busy: false, changed: false, ...sd } : null });

async function pick(bar: HTMLElement, name: string, bytes: Uint8Array, size?: number) {
  const input = $(bar, "sd-file") as HTMLInputElement;
  const file = new File([new Uint8Array(bytes)], name);
  if (size !== undefined) Object.defineProperty(file, "size", { value: size }); // a big one, without the bytes
  Object.defineProperty(input, "files", { configurable: true, value: [file] });
  input.dispatchEvent(new Event("change"));
  await flush();
}

function make() {
  const port = new FakePort();
  const downloads: [string, Uint8Array][] = [];
  const listeners: ((e: Event) => void)[] = [];
  const asked: string[] = [];
  const answer = { yes: true };
  const win = {
    addEventListener: (_: string, l: (e: Event) => void) => listeners.push(l),
    confirm: (q: string) => (asked.push(q), answer.yes),
  } as unknown as Window;
  const bar = sdBar(port, (name, bytes) => downloads.push([name, bytes]), win);
  port.onFrame(bar.update);
  /** What leaving the page would do: true if it asks first. */
  const leaving = () => {
    const e = new Event("beforeunload", { cancelable: true });
    for (const l of listeners) l(e);
    return e.defaultPrevented;
  };
  return { port, bar, downloads, leaving, asked, answer };
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

  test("the page starts the machine off: the row shows once connected, from the slot itself, and a card can go in", async () => {
    const { port, bar } = make();
    port.sdSlot = true;
    port.emit({ ...frame(null), powered: false }); // off: a frame says nothing of the card
    expect(bar.element.hidden).toBe(true);
    await bar.connected();
    expect(bar.element.hidden).toBe(false);
    port.emit({ ...frame(null), powered: false });
    expect(bar.element.hidden).toBe(false);
    await pick(bar.element, "card.img", new Uint8Array(1024));
    expect(port.callsTo("insertSd")).toHaveLength(1);
  });

  test("a machine without the slot: still hidden once connected", async () => {
    const { bar } = make();
    await bar.connected();
    expect(bar.element.hidden).toBe(true);
  });

  test("a card picked goes in, says how to use it, and Eject takes it out", async () => {
    const { port, bar } = make();
    expect(shown(bar.element)).toEqual(["sd-insert"]);
    await pick(bar.element, "card.img", new Uint8Array(2880 * 512));
    expect(port.callsTo("insertSd").map(([b, n]) => [(b as Uint8Array).length, n])).toEqual([[2880 * 512, "card.img"]]);
    expect($(bar.element, "sd-name").textContent).toBe("card.img");
    expect($(bar.element, "sd-meta").textContent).toBe("1440 KB");
    expect($(bar.element, "sd-note").textContent).toContain("C809R");
    expect(shown(bar.element)).toEqual(["sd-protect", "sd-save", "sd-eject"]);
    $(bar.element, "sd-eject").click();
    expect(port.callsTo("ejectSd")).toEqual([[]]);
    expect(shown(bar.element)).toEqual(["sd-insert"]);
    expect($(bar.element, "sd-meta").textContent).toBe("empty");
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
    expect($(bar.element, "sd-note").textContent).toBe("Written to: Save to keep it, as the page forgets it");
    expect($(bar.element, "sd-meta").textContent).toBe("0.5 KB · changed since loaded");
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

  test("a card over 1 GB is refused before it is read: the core's memory stops at 2 GB", async () => {
    const { port, bar } = make();
    await pick(bar.element, "big.img", new Uint8Array(512), MAX_CARD + 512);
    expect(port.callsTo("insertSd")).toEqual([]);
    expect($(bar.element, "sd-note").textContent).toBe("big.img: over 1 GB, more than this page can hold");
    await pick(bar.element, "edge.img", new Uint8Array(512), MAX_CARD); // 1 GB itself goes in
    expect(port.callsTo("insertSd").length).toBe(1);
  });

  test("Eject and another card ask first when the writes aren't saved, and No keeps the card", async () => {
    const { port, bar, asked, answer } = make();
    await pick(bar.element, "card.img", new Uint8Array(512));
    $(bar.element, "sd-eject").click(); // nothing written: no question
    expect(asked).toEqual([]);
    expect(port.callsTo("ejectSd").length).toBe(1);

    await pick(bar.element, "card.img", new Uint8Array(512));
    port.emit(frame({ changed: true }));
    answer.yes = false;
    $(bar.element, "sd-eject").click();
    await pick(bar.element, "other.img", new Uint8Array(512));
    expect(asked.length).toBe(2);
    expect(asked[0]).toContain("Eject the card?");
    expect(asked[1]).toContain("Put other.img in?");
    expect(asked[1]).toContain("card.img aren't saved");
    expect(port.callsTo("ejectSd").length).toBe(1); // kept
    expect(port.callsTo("insertSd").length).toBe(2);
    expect($(bar.element, "sd-name").textContent).toBe("card.img");

    answer.yes = true;
    await pick(bar.element, "other.img", new Uint8Array(1024));
    expect(asked.length).toBe(3);
    expect($(bar.element, "sd-name").textContent).toBe("other.img");
    expect($(bar.element, "sd-meta").textContent).toBe("1 KB");
  });

  test("its pill: the card's size and light, while one is in; it opens Settings", async () => {
    const port = new FakePort();
    let opened = 0;
    const win = { addEventListener: () => {}, confirm: () => true } as unknown as Window;
    const bar = sdBar(port, () => {}, win, { open: () => ++opened });
    port.onFrame(bar.update);
    port.emit(frame({ present: false }));
    expect(bar.pill.hidden).toBe(true);
    await pick(bar.element, "card.img", new Uint8Array(65536 * 512), 65536 * 512);
    port.emit(frame({ busy: true }));
    expect(bar.pill.hidden).toBe(false);
    expect(bar.pill.textContent).toBe("SD 32 MB");
    expect(bar.pill.querySelector(".led")!.classList.contains("on")).toBe(true);
    bar.pill.click();
    expect(opened).toBe(1);
    $(bar.element, "sd-eject").click();
    expect(bar.pill.hidden).toBe(true);
  });

  test("unsaved: the card's name while it has writes; waived, leaving doesn't ask", async () => {
    const { port, bar, leaving } = make();
    port.emit(frame({ present: false }));
    await pick(bar.element, "card.img", new Uint8Array(1024));
    expect(bar.unsaved()).toBeNull();
    port.emit(frame({ changed: true }));
    expect(bar.unsaved()).toBe("card.img");
    expect(leaving()).toBe(true);
    bar.waive(); // the page asked already
    expect(leaving()).toBe(false);
  });

  test("the keyboard follows the row: Insert… to Eject once a card is in", async () => {
    const { bar } = make();
    document.body.append(bar.element);
    $(bar.element, "sd-insert").focus();
    await pick(bar.element, "card.img", new Uint8Array(1024));
    expect(document.activeElement).toBe($(bar.element, "sd-eject"));
    $(bar.element, "sd-eject").click();
    expect(document.activeElement).toBe($(bar.element, "sd-insert"));
    bar.element.remove();
  });

  test("power off: the row and the pill stay, the light out", async () => {
    const { port, bar } = make();
    port.emit(frame({ present: false }));
    await pick(bar.element, "card.img", new Uint8Array(1024));
    port.emit(frame({ busy: true }));
    port.emit({ powered: false, sd: null }); // the port has no card to report while off
    expect(bar.element.hidden).toBe(false);
    expect(bar.pill.hidden).toBe(false);
    expect(bar.pill.querySelector(".led")!.classList.contains("on")).toBe(false);
  });

  test("the demo card goes in an empty slot, says so, and Insert… stays for a card of your own", async () => {
    const { port, bar } = make();
    await bar.demo(async () => new Uint8Array(65536 * 512));
    expect(port.callsTo("insertSd").map(([b, n]) => [(b as Uint8Array).length, n])).toEqual([[65536 * 512, DEMO_NAME]]);
    expect($(bar.element, "sd-name").textContent).toBe(DEMO_NAME);
    expect($(bar.element, "sd-meta").textContent).toBe("32 MB");
    expect($(bar.element, "sd-note").textContent).toContain("power-on boots OODOS");
    expect(shown(bar.element)).toEqual(["sd-insert", "sd-protect", "sd-save", "sd-eject"]);
    await pick(bar.element, "mine.img", new Uint8Array(2880 * 512)); // yours takes its place
    expect(port.callsTo("insertSd").map(([, n]) => n)).toEqual([DEMO_NAME, "mine.img"]);
    expect($(bar.element, "sd-name").textContent).toBe("mine.img");
    expect($(bar.element, "sd-note").textContent).toContain("C809R");
    expect(shown(bar.element)).toEqual(["sd-protect", "sd-save", "sd-eject"]); // and Insert… goes, as for any card
  });

  test("Eject takes the demo card out: the slot empty, as for any card", async () => {
    const { port, bar } = make();
    await bar.demo(async () => new Uint8Array(1024));
    $(bar.element, "sd-eject").click();
    expect(port.callsTo("ejectSd")).toEqual([[]]);
    expect(shown(bar.element)).toEqual(["sd-insert"]);
    expect($(bar.element, "sd-meta").textContent).toBe("empty");
  });

  test("the demo card isn't put over a card already in, and one that won't load says why", async () => {
    const { port, bar } = make();
    await pick(bar.element, "mine.img", new Uint8Array(1024));
    await bar.demo(async () => new Uint8Array(1024));
    expect(port.callsTo("insertSd").map(([, n]) => n)).toEqual(["mine.img"]);
    const empty = make();
    await empty.bar.demo(async () => {
      throw new Error("404 Not Found");
    });
    expect(empty.port.callsTo("insertSd")).toHaveLength(0);
    expect($(empty.bar.element, "sd-note").textContent).toBe("The demo card didn't load: 404 Not Found");
    expect(shown(empty.bar.element)).toEqual(["sd-insert"]);
  });

  test("the demo card's fetch: gzipped, or sent already unzipped; a failed one throws", async () => {
    const card = new Uint8Array(4096).map((_, i) => (i * 7) & 0xff);
    const get = (body: Uint8Array, status = 200) => (async () => new Response(new Uint8Array(body), { status, statusText: status === 200 ? "OK" : "Not Found" })) as unknown as typeof fetch;
    expect(await fetchDemoCard("x", get(gzipSync(card)))).toEqual(card);
    expect(await fetchDemoCard("x", get(card))).toEqual(card);
    await expect(fetchDemoCard("x", get(new Uint8Array(0), 404))).rejects.toThrow("404 Not Found");
  });

  test("with no card, frames change nothing and leaving doesn't ask", () => {
    const { port, bar, leaving } = make();
    port.emit(frame({ present: false, changed: true }));
    expect(shown(bar.element)).toEqual(["sd-insert"]);
    expect(leaving()).toBe(false);
  });
});
