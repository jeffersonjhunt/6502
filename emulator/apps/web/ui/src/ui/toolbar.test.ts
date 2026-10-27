// @vitest-environment happy-dom
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import { FakePort } from "../port/fake-port";
import { PanelState } from "./panel-state";
import { toolbar } from "./toolbar";

function bar(home?: string) {
  const port = new FakePort();
  const tb = toolbar(port, new PanelState(port), { caps: () => {}, home });
  document.body.replaceChildren(tb.element);
  return tb;
}

const ids = (el: Element) => [...el.children].map((c) => c.getAttribute("data-testid") ?? c.className);

describe("toolbar", () => {
  beforeEach(() => document.body.replaceChildren());
  afterEach(() => vi.unstubAllGlobals());

  test("no home: the name, no link", () => {
    const el = bar().element;
    expect(el.querySelector("a")).toBeNull();
    expect(el.querySelector(".ttl")?.textContent).toBe("OO-1One Off One");
  });

  test("a home: the back link goes to it", () => {
    const a = bar("/projects/oo-1/").element.querySelector<HTMLAnchorElement>('[data-testid="tb-home"]');
    expect(a?.getAttribute("href")).toBe("/projects/oo-1/");
    expect(a?.getAttribute("aria-label")).toBe("Back to the project page");
  });

  test("one row: run, step and clock, then CAPS, Sound, Settings, Debugger; the phosphor is Settings' now", () => {
    vi.stubGlobal("matchMedia", () => ({ matches: false, addEventListener() {} })); // a wide window
    const tb = bar();
    expect(ids(tb.element)).toEqual(["ttl", "sep", "tb-run", "tb-halt", "tb-step", "tb-step-cycle", "sep", "tb-clock", "grow", "tb-caps", "sep", "tb-settings", "tb-debugger"]);
    expect(tb.element.querySelector('[aria-label="Phosphor"]')).toBeNull();
    expect(tb.element.querySelector('[data-testid="tb-more"]')).toBeNull();
  });

  test("on a phone: Run or Halt, Sound, Settings and More; More holds the rest, and Escape folds it", () => {
    vi.stubGlobal("matchMedia", (q: string) => ({ matches: q.includes("600px"), addEventListener() {} }));
    const port = new FakePort();
    const sound = document.createElement("button");
    sound.dataset.testid = "tb-sound";
    const tb = toolbar(port, new PanelState(port), { caps: () => {}, sound, home: "/" });
    document.body.replaceChildren(tb.element);
    expect(ids(tb.element)).toEqual(["tb-home", "ttl", "grow", "tb-run", "tb-halt", "tb-sound", "tb-settings", "tb-more"]);
    const more = tb.element.querySelector<HTMLButtonElement>('[data-testid="tb-more"]')!;
    const menu = tb.element.querySelector<HTMLElement>('[data-testid="tb-menu"]')!;
    expect(ids(menu)).toEqual(["tb-step", "tb-step-cycle", "tb-clock", "tb-caps", "tb-debugger"]);
    expect(menu.hidden).toBe(true);
    more.click();
    expect(menu.hidden).toBe(false);
    expect(more.getAttribute("aria-expanded")).toBe("true");
    expect(tb.focusBack(tb.debug)).toBe(tb.debug); // open: the button itself
    menu.querySelector("button")!.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    expect(menu.hidden).toBe(true);
    expect(document.activeElement).toBe(more);
    expect(tb.focusBack(tb.debug)).toBe(more); // folded away: More stands in for it
    more.click();
    tb.debug.click(); // the drawer takes over
    expect(menu.hidden).toBe(true);
  });

  test("between a phone and a wide window: the steps and the clock under More, the rest in the row", () => {
    vi.stubGlobal("matchMedia", (q: string) => ({ matches: q.includes("1279px"), addEventListener() {} }));
    const tb = bar();
    expect(ids(tb.element)).toEqual(["ttl", "sep", "tb-run", "tb-halt", "grow", "tb-caps", "sep", "tb-settings", "tb-debugger", "tb-more"]);
    const menu = tb.element.querySelector<HTMLElement>('[data-testid="tb-menu"]')!;
    expect(ids(menu)).toEqual(["tb-step", "tb-step-cycle", "tb-clock"]);
    expect(tb.element.querySelector('[data-testid="tb-more"]')!.getAttribute("aria-label")).toBe("More: step, clock");
  });

  test("CAPS is a toggle: pressed, and remembered", () => {
    const tb = bar();
    const caps = tb.element.querySelector<HTMLButtonElement>('[data-testid="tb-caps"]')!;
    expect(caps.getAttribute("aria-pressed")).toBe("true");
    caps.click();
    expect(caps.getAttribute("aria-pressed")).toBe("false");
    expect(localStorage.getItem("oo1.caps")).toBe("off");
  });
});
