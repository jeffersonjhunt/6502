// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { h, prefs } from "./dom";
import { drawer, type DrawerId } from "./drawer";

function make(phone = false) {
  const shown: [DrawerId, boolean][] = [];
  const settingsButton = h("button", {}, "Settings");
  const debuggerButton = h("button", {}, "Debugger");
  const settings = h("aside", { "aria-label": "Settings" }, h("h2", {}, "Settings"), h("button", { id: "inside" }, "Load…"));
  const debug = h("aside", { "aria-label": "Debugger" }, "tabs");
  const page = h("main", {});
  const listeners: (() => void)[] = [];
  const media = { matches: phone, addEventListener: (_: string, l: () => void) => listeners.push(l) } as unknown as MediaQueryList;
  const d = drawer(
    {
      settings: { element: settings, button: settingsButton, label: "Settings", shown: (o) => shown.push(["settings", o]) },
      debugger: { element: debug, button: debuggerButton, label: "Debugger", shown: (o) => shown.push(["debugger", o]) },
    },
    { phone: media, behind: [page] },
  );
  document.body.replaceChildren(settingsButton, debuggerButton, page, d.element);
  return { d, settings, debug, settingsButton, debuggerButton, page, shown, media, listeners };
}

describe("the drawer", () => {
  beforeEach(() => {
    localStorage.clear();
    document.body.className = "";
  });

  test("one panel at a time: opening one closes the other; each button toggles its own, pressed while it shows", () => {
    const { d, settings, debug, settingsButton, debuggerButton, shown } = make();
    expect(d.element.hidden).toBe(true);
    settingsButton.click();
    expect(settings.hidden).toBe(false);
    expect(settingsButton.getAttribute("aria-pressed")).toBe("true");
    expect(document.body.classList.contains("drawer-open")).toBe(true);
    debuggerButton.click();
    expect(settings.hidden).toBe(true);
    expect(debug.hidden).toBe(false);
    expect(settingsButton.getAttribute("aria-pressed")).toBe("false");
    expect(debuggerButton.getAttribute("aria-pressed")).toBe("true");
    expect(shown).toEqual([["settings", true], ["settings", false], ["debugger", true]]);
    debuggerButton.click();
    expect(d.element.hidden).toBe(true);
    expect(document.body.classList.contains("drawer-open")).toBe(false);
    expect(shown.at(-1)).toEqual(["debugger", false]);
  });

  test("focus goes in when it opens, back to its button when it closes; Escape closes it from inside", () => {
    const { d, settings, settingsButton } = make();
    settingsButton.click();
    expect(document.activeElement).toBe(settings);
    (settings.querySelector("#inside") as HTMLElement).focus();
    settings.querySelector("#inside")!.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    expect(d.current()).toBeNull();
    expect(document.activeElement).toBe(settingsButton);
    d.show("settings", { at: settings.querySelector("h2")! });
    expect(document.activeElement).toBe(settings.querySelector("h2"));
    d.closers.settings.click();
    expect(d.current()).toBeNull();
  });

  test("a region beside the page; on a phone a modal dialog, the page behind it inert", () => {
    const wide = make();
    wide.settingsButton.click();
    expect(wide.settings.getAttribute("role")).toBe("region");
    expect(wide.page.inert).toBe(false);
    const phone = make(true);
    phone.settingsButton.click();
    expect(phone.settings.getAttribute("role")).toBe("dialog");
    expect(phone.settings.getAttribute("aria-modal")).toBe("true");
    expect(phone.page.inert).toBe(true);
    phone.d.hide();
    expect(phone.page.inert).toBe(false);
  });

  test("the debugger starts closed on a first visit; left open, it opens again without taking focus", () => {
    const first = make();
    first.d.restore();
    expect(first.d.current()).toBeNull();
    first.debuggerButton.click();
    expect(prefs.get("dbg")).toBe("open");
    const again = make();
    document.body.focus();
    again.d.restore();
    expect(again.d.current()).toBe("debugger");
    expect(document.activeElement).not.toBe(again.debug);
    again.settingsButton.click(); // Settings in its place: the debugger is closed, for next time too
    expect(prefs.get("dbg")).toBe("closed");
  });
});
