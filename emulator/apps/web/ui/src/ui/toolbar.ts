/**
 * The bar across the top, one row: back to the project page, the name,
 * Run / Halt / Step through the panel, the clock; at the right, CAPS for
 * the keyboard, Sound, and the Settings and Debugger toggles (the drawer
 * binds them, ui/drawer.ts).
 *
 * On a phone (PHONE) the row keeps back, the name, Run or Halt (whichever
 * the machine isn't doing), Sound and Settings, and the rest go under More:
 * Step inst, Step cycle, the clock, CAPS and Debugger. Between a phone
 * and a wide window (MIDDLE) More holds the steps and the clock, and the
 * rest stays in the row; base.css drops the subtitle, then the toggles'
 * words, as it narrows. The same buttons, moved: they keep their state
 * and listeners.
 */

import { CLOCKS_HZ, type Clock, type FrameState, type MachinePort } from "../port/machine-port";
import { h, prefs, segmented } from "./dom";
import { icon } from "./icons";
import type { PanelState } from "./panel-state";

/** Where the page is laid out for a phone. */
export const PHONE = "(max-width: 600px)";
/** Where the whole row no longer fits (measured: it wraps below 1270px): the steps and the clock fold into More. */
export const MIDDLE = "(max-width: 1279px)";

export interface ToolbarHooks {
  caps: (on: boolean) => void;
  /** Sound's switch (ui/sound-button.ts), at the right with the toggles. */
  sound?: HTMLElement;
  /** Where the back link goes (the project page), if anywhere. */
  home?: string;
}

const CLOCK_TEXT = ["1.02", "1.79", "3.58", "7.16 MHz"];

/** A button whose text hides on a phone, leaving the icon (and the name, for a screen reader). */
function toggle(name: "settings" | "debugger", text: string, title: string) {
  return h("button", { class: "tbtn", type: "button", "aria-pressed": "false", title, "data-testid": `tb-${name}` }, icon(name), h("span", { class: "txt" }, text));
}

export function toolbar(port: MachinePort, panel: PanelState, hooks: ToolbarHooks) {
  const back = hooks.home
    ? h("a", { class: "home", href: hooks.home, "aria-label": "Back to the project page", title: "The OO-1 project page", "data-testid": "tb-home" }, icon("back"))
    : null;
  const ttl = h("div", { class: "ttl" }, "OO-1", h("span", {}, "One Off One"));
  const run = h("button", { class: "tbtn", type: "button", title: "Run: HALT to ENABLE, then CONT", "data-testid": "tb-run" }, icon("run"), "Run");
  const halt = h("button", { class: "tbtn", type: "button", title: "Halt: the HALT switch", "data-testid": "tb-halt" }, icon("halt"), "Halt");
  const stepInst = h("button", { class: "tbtn", type: "button", title: "Step one instruction (S INST, CONT)", "data-testid": "tb-step" }, "Step inst");
  const stepCycle = h(
    "button",
    {
      class: "tbtn",
      type: "button",
      disabled: !port.caps.busCycleStep,
      title: port.caps.busCycleStep ? "Step one bus cycle" : "Bus-cycle step: the emulator steps whole instructions",
      "data-testid": "tb-step-cycle",
    },
    "Step cycle",
  );
  run.addEventListener("click", () => panel.run());
  halt.addEventListener("click", () => panel.halt());
  stepInst.addEventListener("click", () => panel.step());

  const savedClock = prefs.get("clock");
  const clock: Clock = savedClock === "max" ? "max" : CLOCKS_HZ.find((c) => String(c) === savedClock) ?? CLOCKS_HZ[3];
  port.setClock(clock);
  let picked = String(clock); // the user's pick, shown again when nothing fixes the clock
  const clocks = segmented<string>(
    "Clock",
    [
      ...CLOCKS_HZ.map((hz, i) => ({ value: String(hz), text: CLOCK_TEXT[i], title: `${(hz / 1e6).toFixed(5)} MHz` })),
      { value: "max", text: "Max", title: "As fast as this machine can" },
    ],
    String(clock),
    (v) => {
      picked = v;
      port.setClock(v === "max" ? "max" : Number(v));
      prefs.set("clock", v);
    },
  );
  const clockGroup = h("span", { class: "tb-clock" }, h("span", { class: "lbl" }, "Clock"), clocks.element);

  let capsOn = prefs.get("caps") !== "off";
  const caps = h("button", { class: "tbtn", type: "button", "aria-pressed": "true", title: "Send letters as capitals (the Woz Monitor wants them)", "data-testid": "tb-caps" }, "CAPS");
  const showCaps = () => {
    caps.classList.toggle("on", capsOn);
    caps.setAttribute("aria-pressed", String(capsOn));
  };
  caps.addEventListener("click", () => {
    capsOn = !capsOn;
    showCaps();
    hooks.caps(capsOn);
    prefs.set("caps", capsOn ? "on" : "off");
  });
  showCaps();
  hooks.caps(capsOn);

  const settings = toggle("settings", "Settings", "Settings: ROMs, disks, the SD card, sound, display");
  const debug = toggle("debugger", "Debugger", "Show or hide the debugger");

  // More, on a phone: a disclosure holding the rest of the row.
  const menu = h("div", { class: "tb-menu", id: "tb-menu", hidden: true, "data-testid": "tb-menu" });
  const more = h(
    "button",
    { class: "tbtn icon", type: "button", "aria-expanded": "false", "aria-controls": "tb-menu", "aria-label": "More: step, clock, caps, debugger", "data-testid": "tb-more" },
    icon("more"),
  );
  const setMenu = (open: boolean) => {
    menu.hidden = !open;
    more.setAttribute("aria-expanded", String(open));
  };
  more.addEventListener("click", () => setMenu(menu.hidden === true));
  menu.addEventListener("keydown", (e) => {
    if (e.key !== "Escape") return;
    e.preventDefault();
    setMenu(false);
    more.focus();
  });
  debug.addEventListener("click", () => setMenu(false)); // the drawer takes over
  document.addEventListener("pointerdown", (e) => {
    if (!menu.hidden && !menu.contains(e.target as Node) && !more.contains(e.target as Node)) setMenu(false);
  });

  const element = h("header", { class: "tb" });
  const sep = () => h("span", { class: "sep" });
  const grow = () => h("span", { class: "grow" });
  const sound = hooks.sound ? [hooks.sound] : [];
  const phone = typeof matchMedia === "function" ? matchMedia(PHONE) : null;
  const middle = typeof matchMedia === "function" ? matchMedia(MIDDLE) : null;
  const folded = () => h("span", { class: "tb-more" }, more, menu);
  const layout = () => {
    const lead = back ? [back, ttl] : [ttl];
    if (phone?.matches) {
      menu.replaceChildren(stepInst, stepCycle, clockGroup, caps, debug);
      more.setAttribute("aria-label", "More: step, clock, caps, debugger");
      element.replaceChildren(...lead, grow(), run, halt, ...sound, settings, folded());
    } else if (middle?.matches) {
      menu.replaceChildren(stepInst, stepCycle, clockGroup);
      more.setAttribute("aria-label", "More: step, clock");
      element.replaceChildren(...lead, sep(), run, halt, grow(), caps, ...sound, sep(), settings, debug, folded());
    } else {
      setMenu(false);
      element.replaceChildren(...lead, sep(), run, halt, stepInst, stepCycle, sep(), clockGroup, grow(), caps, ...sound, sep(), settings, debug);
    }
  };
  phone?.addEventListener("change", layout);
  middle?.addEventListener("change", layout);
  layout();

  // Run, Halt and Step are the panel's own controls: dead unless the keyswitch is at RUN.
  const live = () => {
    for (const b of [run, halt, stepInst]) b.disabled = !panel.live;
  };
  panel.onChange(live);
  live();

  /** Light Run or Halt to match the machine; while its mode fixes the clock, show that one, locked. */
  let fixedShown = 0;
  const update = (f: FrameState) => {
    if (f.fixedHz !== fixedShown) {
      fixedShown = f.fixedHz;
      const near = CLOCKS_HZ.find((c) => Math.abs(c - f.fixedHz) < 1000);
      clocks.select(fixedShown ? (near !== undefined ? String(near) : "") : picked);
      for (const b of clocks.element.querySelectorAll("button")) b.disabled = fixedShown !== 0;
      clocks.element.title = fixedShown ? "Apple II mode: the Apple's own clock, 1.02 MHz" : "";
    }
    const running = f.powered && !f.halted && !f.stopped;
    run.classList.toggle("on", running);
    halt.classList.toggle("on", f.powered && f.halted);
  };
  /** CAPS is the serial terminal's; on the USB keyboard the card has its own Caps Lock. */
  const setCapsEnabled = (on: boolean) => {
    caps.disabled = !on;
    caps.title = on ? "Send letters as capitals (the Woz Monitor wants them)" : "The USB keyboard has its own Caps Lock (on at power-up)";
  };
  /** Where focus goes back to from the drawer: `button`, or More if it is folded away in the menu. */
  const focusBack = (button: HTMLElement) => {
    if (menu.contains(button) && menu.hidden) return more;
    return button;
  };
  return { element, update, setCapsEnabled, settings, debug, focusBack };
}
