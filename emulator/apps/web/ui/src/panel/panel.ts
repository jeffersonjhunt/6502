/**
 * The front panel in its 19-inch rack unit (mockup C), wired to the
 * machine: the Console card's lamps and switches (hw/design.md, "Front
 * Panel").
 *
 * Lamps: A0-A19 (the physical address, after the north bridge), D0-D7, and
 * POWER, RUN, HALT, SYNC, RWB, VPB, RDY, BE, IRQ, AMODE. Each glows with
 * the share of bus cycles its line was high, as a running PDP-11's did,
 * and freezes on the latched cycle when halted. BE and AMODE stay dark:
 * neither is modelled yet.
 *
 * Switches: the 20-bit switch register, in hex digits of four, orange and
 * gold; LOAD ADRS, EXAM, DEP, CONT and START are momentary; HALT/ENABLE and
 * S INST/S BUS CYCLE stay where they are put. The keyswitch turns OFF, RUN,
 * LOCK. All of it goes through PanelState.
 */

import mark from "../assets/ooe-makers-mark.png";
import type { FrameState } from "../port/machine-port";
import type { PanelKey } from "../port/machine-port";
import { h } from "../ui/dom";
import type { PanelState, PanelSwitches } from "../ui/panel-state";

/** Lamp brightness from duty: incandescent, so a line high a little still glows a little. */
export function glow(duty: number): number {
  return duty <= 0 ? 0 : Math.min(1, 0.12 + 0.88 * Math.pow(duty, 0.6));
}

const STATUS = [
  ["POWER", "Power"],
  ["RUN", "Run"],
  ["HALT", "Halt"],
  ["SYNC", "SYNC: opcode fetch"],
  ["RWB", "RWB: lit on a read"],
  ["VPB", "VPB: vector pull"],
  ["RDY", "RDY: wait state, or held"],
  ["BE", "BE: DMA or the panel owns the bus (not modelled)"],
  ["IRQ", "IRQ asserted"],
  ["AMODE", "Apple II mode (not modelled)"],
] as const;
type StatusName = (typeof STATUS)[number][0];

interface Control {
  id: PanelKey | "halt" | "step";
  top: string;
  bot: string;
  kind: "mom" | "tog";
  tip: string;
}
const CONTROLS: Control[] = [
  { id: "load", top: "LOAD", bot: "ADRS", kind: "mom", tip: "LOAD ADRS: the switch register (20 bits) to the panel address" },
  { id: "exam", top: "EXAM", bot: "", kind: "mom", tip: "EXAM: read the panel address (halted); again to step on" },
  { id: "dep", top: "DEP", bot: "", kind: "mom", tip: "DEP: deposit switch register bits 7-0 (halted); again to step on" },
  { id: "cont", top: "CONT", bot: "", kind: "mom", tip: "CONT: continue, or one step while HALT is down" },
  { id: "halt", top: "ENABLE", bot: "HALT", kind: "tog", tip: "HALT / ENABLE" },
  { id: "step", top: "S INST", bot: "S BUS", kind: "tog", tip: "S INST / S BUS CYCLE: what CONT steps (the emulator steps instructions)" },
  { id: "start", top: "START", bot: "", kind: "mom", tip: "START: hard reset through the north bridge" },
];

/** Grid column of address bit `bit` (and of data bit `bit`, under it). */
function col(bit: number): number {
  const g = (19 - bit) >> 2;
  return 1 + g * 5 + ((19 - bit) & 3);
}
const CTL_COL = 26;

function at<T extends HTMLElement>(e: T, column: number, row: number, span?: number): T {
  e.style.gridColumn = span ? `${column} / span ${span}` : String(column);
  e.style.gridRow = String(row);
  return e;
}

const hex = (v: number, n: number) => v.toString(16).toUpperCase().padStart(n, "0");

class Lamp {
  readonly element: HTMLElement;
  private b = -1;
  constructor(name: string, tip: string) {
    this.element = h("span", { class: "lamp", role: "img", "aria-label": tip, title: tip, "data-lamp": name }, h("i"));
  }
  set(b: number, halted: boolean): void {
    const q = Math.round(b * 100) / 100;
    if (q === this.b) return;
    const wasLive = this.element.classList.contains("live");
    this.b = q;
    this.element.style.setProperty("--b", q.toFixed(2));
    const live = q > 0.13 && q < 0.97 && !halted;
    this.element.classList.toggle("live", live);
    if (live && !wasLive) this.element.style.setProperty("--fd", `${(0.07 + Math.random() * 0.25).toFixed(2)}s`);
    this.element.classList.toggle("on", q > 0.5);
  }
  get brightness(): number {
    return this.b;
  }
}

export interface FrontPanel {
  element: HTMLElement;
  update(frame: FrameState): void;
}

export function frontPanel(panel: PanelState): FrontPanel {
  const grid = h("div", { class: "pgrid", role: "group", "aria-label": "Lamps and switches" });
  const cols: string[] = [];
  for (let g = 0; g < 5; g++) {
    for (let p = 0; p < 4; p++) cols.push("var(--cell)");
    cols.push(g < 4 ? "var(--ggap)" : "var(--sgap)");
  }
  for (let c = 0; c < CONTROLS.length; c++) cols.push("var(--ctl)");
  grid.style.gridTemplateColumns = cols.join(" ");

  for (let g = 0; g < 5; g++) {
    // a shaded band behind each hex digit, top to bottom
    const band = h("div", { class: `band band${g % 2}` });
    band.style.gridColumn = `${1 + g * 5} / span 4`;
    band.style.gridRow = "1 / -1";
    grid.append(band);
  }

  const addr: Lamp[] = [];
  const data: Lamp[] = [];
  const status = new Map<StatusName, Lamp>();
  for (let bit = 19; bit >= 0; bit--) {
    grid.append(at(h("span", { class: "bitn" }, String(bit)), col(bit), 1));
    addr[bit] = new Lamp(`A${bit}`, `A${bit}: physical address bit ${bit}`);
    grid.append(at(addr[bit].element, col(bit), 2));
  }
  grid.append(at(h("span", { class: "cap cap-addr" }, "ADDRESS"), 1, 3, 14));
  for (let bit = 7; bit >= 0; bit--) {
    grid.append(at(h("span", { class: "bitn" }, String(bit)), col(bit), 3));
    data[bit] = new Lamp(`D${bit}`, `D${bit}: data bit ${bit}`);
    grid.append(at(data[bit].element, col(bit), 4));
  }
  grid.append(at(h("span", { class: "cap cap-data" }, "DATA"), col(7), 5, 9));
  STATUS.forEach(([name, tip], i) => {
    const row = i < 5 ? 1 : 3;
    const c = CTL_COL + (i % 5);
    grid.append(at(h("span", { class: "slab" }, name), c, row));
    const lamp = new Lamp(name, tip);
    status.set(name, lamp);
    grid.append(at(lamp.element, c, row + 1));
  });
  grid.append(at(h("div", { class: "divider" }), 1, 6, 32));

  // The switch register.
  const swr: HTMLButtonElement[] = [];
  for (let bit = 19; bit >= 0; bit--) {
    grid.append(at(h("span", { class: "bitn" }, String(bit)), col(bit), 7));
    const sw = h(
      "button",
      {
        class: `sw sr ${((19 - bit) >> 2) % 2 ? "gold" : "orange"}`,
        type: "button",
        "aria-label": `Switch register bit ${bit}${bit < 8 ? " (DEP data)" : ""}`,
        "data-bit": String(bit),
      },
      h("span", { class: "paddle" }),
    );
    sw.addEventListener("click", () => panel.set({ swr: panel.switches.swr ^ (1 << bit) }));
    swr[bit] = sw;
    grid.append(at(sw, col(bit), 8));
  }
  grid.append(at(h("span", { class: "cap cap-swr" }, h("b"), h("em", {}, "SWITCH REGISTER · LOAD ADRS"), h("b")), 1, 9, 24));
  grid.append(at(h("span", { class: "cap cap-dep" }, h("b"), h("em", {}, "DEP DATA"), h("b")), col(7), 10, 9));

  // The keys and toggles.
  const toggles = new Map<string, HTMLButtonElement>();
  CONTROLS.forEach((k, i) => {
    grid.append(at(h("span", { class: "clab" }, k.top, ...(k.bot ? [h("br"), k.bot] : [])), CTL_COL + i, 7));
    const b = h(
      "button",
      { class: `sw ctl ${k.kind} k-${k.id}`, type: "button", title: k.tip, "aria-label": k.tip, "data-ctl": k.id },
      h("span", { class: "paddle" }),
    );
    if (k.kind === "mom") {
      b.addEventListener("pointerdown", () => {
        if (!panel.live) return;
        b.classList.add("down");
        const up = () => {
          b.classList.remove("down");
          window.removeEventListener("pointerup", up);
        };
        window.addEventListener("pointerup", up);
      });
      b.addEventListener("click", () => panel.press(k.id as PanelKey));
    } else {
      toggles.set(k.id, b);
      b.addEventListener("click", () => {
        const sw = panel.switches;
        panel.set(k.id === "halt" ? { halt: !sw.halt } : { busCycle: !sw.busCycle });
      });
    }
    grid.append(at(b, CTL_COL + i, 8));
  });

  // Brand, keyswitch and readouts.
  const key = h("button", { class: "keysw", type: "button", "data-testid": "keyswitch" });
  key.addEventListener("click", () => panel.turn());
  const readSwr = h("span", { "data-testid": "readout-swr" });
  const readAddr = h("span", { "data-testid": "readout-addr" });
  const readData = h("span", { "data-testid": "readout-data" });
  const brand = h(
    "div",
    { class: "brand" },
    h(
      "div",
      {},
      h("div", { class: "mk" }, h("img", { src: mark, alt: "One Off Endeavors maker's mark" })),
      h("div", { class: "lg" }, "OO", h("span", {}, "-"), "1"),
      h("div", { class: "sb" }, "ONE OFF ONE"),
    ),
    h("div", { class: "krow" }, key, h("small", {}, "OFF", h("br"), "RUN", h("br"), "LOCK")),
  );
  const readouts = h(
    "div",
    { class: "readouts", "aria-live": "off" },
    h("span", {}, "SWR ", readSwr),
    h("span", {}, "ADDR ", readAddr),
    h("span", {}, "DATA ", readData),
  );

  const element = h(
    "section",
    { class: "rack", "aria-label": "Front panel, rack mounted", "data-testid": "panel" },
    h("div", { class: "ear l" }, h("i"), h("i")),
    h("div", { class: "unit" }, brand, h("div", { class: "face" }, grid, readouts)),
    h("div", { class: "ear r" }, h("i"), h("i")),
  );

  const showSwitches = (sw: PanelSwitches) => {
    swr.forEach((b, bit) => {
      const on = ((sw.swr >> bit) & 1) === 1;
      b.classList.toggle("up", on);
      b.setAttribute("aria-pressed", String(on));
    });
    const halt = toggles.get("halt")!;
    halt.classList.toggle("up", !sw.halt); // up = ENABLE
    halt.setAttribute("aria-pressed", String(sw.halt));
    const step = toggles.get("step")!;
    step.classList.toggle("up", !sw.busCycle); // up = S INST
    step.setAttribute("aria-pressed", String(sw.busCycle));
    key.dataset.pos = sw.key;
    key.setAttribute("aria-label", `Keyswitch at ${sw.key.toUpperCase()}: click to turn`);
    document.body.dataset.key = sw.key;
    readSwr.textContent = hex(sw.swr, 5);
  };
  panel.onChange(showSwitches);
  showSwitches(panel.switches);

  const update = (f: FrameState) => {
    const halted = f.powered && f.halted;
    const l = f.powered ? f.lights : null;
    for (let bit = 0; bit < 20; bit++) addr[bit].set(l ? glow(l.addr[bit]) : 0, halted);
    for (let bit = 0; bit < 8; bit++) data[bit].set(l ? glow(l.data[bit]) : 0, halted);
    const running = f.powered && !f.halted && !f.stopped;
    const values: Record<StatusName, number> = {
      POWER: f.powered ? 1 : 0,
      RUN: running ? 1 : 0,
      HALT: halted ? 1 : 0,
      SYNC: l ? glow(l.sync) : 0,
      RWB: l ? glow(l.rwb) : 0,
      VPB: l ? glow(l.vpb) : 0,
      RDY: l ? glow(l.rdy) : 0,
      BE: 0,
      IRQ: f.powered && f.irq ? 1 : 0,
      AMODE: 0,
    };
    for (const [name, lamp] of status) lamp.set(values[name], halted);
    if (halted && l) {
      // Halted, the lamps are the latched cycle (or what EXAM/DEP touched): read them out.
      let a = 0;
      let d = 0;
      for (let bit = 0; bit < 20; bit++) if (l.addr[bit] > 0.5) a |= 1 << bit;
      for (let bit = 0; bit < 8; bit++) if (l.data[bit] > 0.5) d |= 1 << bit;
      readAddr.textContent = hex(a, 5);
      readData.textContent = hex(d, 2);
    } else {
      readAddr.textContent = f.powered ? "-----" : "";
      readData.textContent = f.powered ? "--" : "";
    }
  };

  return { element, update };
}
