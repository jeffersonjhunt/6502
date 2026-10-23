/** The CPU tab: PC A X Y S P, the flags, and the cycle count. */

import type { FrameState, Registers } from "../port/machine-port";
import { h } from "../ui/dom";
import { hex } from "./breakpoints";

const FLAGS = ["N", "V", "-", "B", "D", "I", "Z", "C"] as const;
const REGS: [keyof Registers, number][] = [
  ["pc", 4],
  ["a", 2],
  ["x", 2],
  ["y", 2],
  ["s", 2],
  ["p", 2],
];

export function registersView() {
  const cells = new Map<keyof Registers, HTMLElement>();
  const regs = h("div", { class: "regs" });
  for (const [name, width] of REGS) {
    const v = h("span", { "data-testid": `reg-${name}` }, "-".repeat(width));
    cells.set(name, v);
    regs.append(h("div", { class: `reg ${name}` }, h("b", {}, name.toUpperCase()), v));
  }
  const flagEls = FLAGS.map((f) => h("span", { title: f === "-" ? "bit 5: always 1" : `flag ${f}` }, f));
  const cycles = h("em", { "data-testid": "cycles" }, "");
  const element = h(
    "div",
    { class: "sec" },
    h("h4", {}, "Registers ", cycles),
    regs,
    h("div", { class: "flags", "data-testid": "flags" }, ...flagEls),
    h("p", { class: "hint" }, "Live while the CPU runs; exact between instructions when halted."),
  );

  const update = (f: FrameState) => {
    cycles.textContent = f.powered ? `cycle ${f.cycles.toLocaleString("en-US").replace(/,/g, " ")}` : "power off";
    const r = f.regs;
    if (!r) return;
    for (const [name, width] of REGS) cells.get(name)!.textContent = hex(r[name], width);
    // The live P never has B set (only pushed copies do), and bit 5 always reads 1.
    flagEls.forEach((e, i) => e.classList.toggle("on", ((r.p >> (7 - i)) & 1) === 1));
  };
  return { element, update };
}
