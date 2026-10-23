/**
 * The bar across the top: the badge and name, Run / Halt / Step through
 * the panel, the clock, the phosphor, and CAPS for the keyboard.
 */

import mark from "../assets/ooe-makers-mark.png";
import { PHOSPHORS, type Phosphor } from "../crt/crt";
import { CLOCKS_HZ, type Clock, type FrameState, type MachinePort } from "../port/machine-port";
import { h, prefs, segmented } from "./dom";
import type { PanelState } from "./panel-state";

export interface ToolbarHooks {
  phosphor: (p: Phosphor) => void;
  caps: (on: boolean) => void;
  /** Buttons for the right-hand end (the debugger's toggle). */
  trailing?: HTMLElement[];
}

const CLOCK_TEXT = ["1.02", "1.79", "3.58", "7.16 MHz"];

export function toolbar(port: MachinePort, panel: PanelState, hooks: ToolbarHooks) {
  const run = h("button", { class: "tbtn", type: "button", title: "Run: HALT to ENABLE, then CONT", "data-testid": "tb-run" }, "▶ Run");
  const halt = h("button", { class: "tbtn", type: "button", title: "Halt: the HALT switch", "data-testid": "tb-halt" }, "❚❚ Halt");
  const stepInst = h("button", { class: "tbtn", type: "button", title: "Step one instruction (S INST, CONT)", "data-testid": "tb-step" }, "Step inst");
  const stepCycle = h(
    "button",
    {
      class: "tbtn",
      type: "button",
      disabled: !port.caps.busCycleStep,
      title: port.caps.busCycleStep ? "Step one bus cycle" : "Bus-cycle step: the emulator steps whole instructions",
    },
    "Step cycle",
  );
  run.addEventListener("click", () => panel.run());
  halt.addEventListener("click", () => panel.halt());
  stepInst.addEventListener("click", () => panel.step());

  const savedClock = prefs.get("clock");
  const clock: Clock = savedClock === "max" ? "max" : CLOCKS_HZ.find((c) => String(c) === savedClock) ?? CLOCKS_HZ[3];
  port.setClock(clock);
  const clocks = segmented<string>(
    "Clock",
    [
      ...CLOCKS_HZ.map((hz, i) => ({ value: String(hz), text: CLOCK_TEXT[i], title: `${(hz / 1e6).toFixed(5)} MHz` })),
      { value: "max", text: "Max", title: "As fast as this machine can" },
    ],
    String(clock),
    (v) => {
      port.setClock(v === "max" ? "max" : Number(v));
      prefs.set("clock", v);
    },
  );

  const savedPhosphor = prefs.get("phosphor") as Phosphor | null;
  const phosphor: Phosphor = savedPhosphor && savedPhosphor in PHOSPHORS ? savedPhosphor : "p3";
  const phosphors = segmented<Phosphor>(
    "Phosphor",
    [
      { value: "colour", text: "Colour", swatch: "linear-gradient(90deg,#e33,#3c3,#36f)" },
      { value: "p1", text: "P1", swatch: PHOSPHORS.p1.fg },
      { value: "p3", text: "P3", swatch: PHOSPHORS.p3.fg },
      { value: "p4", text: "P4", swatch: PHOSPHORS.p4.fg },
    ],
    phosphor,
    (p) => {
      hooks.phosphor(p);
      prefs.set("phosphor", p);
    },
  );
  hooks.phosphor(phosphor);

  let capsOn = prefs.get("caps") !== "off";
  const caps = h("button", { class: "tbtn", type: "button", title: "Send letters as capitals (the Woz Monitor wants them)" }, "CAPS");
  const showCaps = () => caps.classList.toggle("on", capsOn);
  caps.addEventListener("click", () => {
    capsOn = !capsOn;
    showCaps();
    hooks.caps(capsOn);
    prefs.set("caps", capsOn ? "on" : "off");
  });
  showCaps();
  hooks.caps(capsOn);

  const element = h(
    "header",
    { class: "tb" },
    h("img", { src: mark, alt: "One Off Endeavors" }),
    h("div", { class: "ttl" }, "OO-1", h("span", {}, "One Off One")),
    run,
    halt,
    stepInst,
    stepCycle,
    h("span", { class: "sep" }),
    h("span", { class: "lbl" }, "Clock"),
    clocks.element,
    h("span", { class: "grow" }),
    caps,
    h("span", { class: "lbl" }, "Phosphor"),
    phosphors.element,
    ...(hooks.trailing?.length ? [h("span", { class: "sep" }), ...hooks.trailing] : []),
  );

  // Run, Halt and Step are the panel's own controls: dead unless the keyswitch is at RUN.
  const live = () => {
    for (const b of [run, halt, stepInst]) b.disabled = !panel.live;
  };
  panel.onChange(live);
  live();

  /** Light Run or Halt to match the machine. */
  const update = (f: FrameState) => {
    const running = f.powered && !f.halted && !f.stopped;
    run.classList.toggle("on", running);
    halt.classList.toggle("on", f.powered && f.halted);
  };
  return { element, update };
}
