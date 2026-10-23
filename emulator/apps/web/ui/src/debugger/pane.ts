/**
 * The debugger pane (mockup C): collapsible, at the right of the bench,
 * with tabs. CPU (registers and flags), Bus trace and Breakpoints work;
 * Memory and Devices come later and show as such.
 *
 * It costs nothing while closed: registers are watched only while the CPU
 * tab is showing and the trace recorded only while its tab is, so the port
 * does that work only when someone is looking.
 */

import type { FrameState, MachinePort } from "../port/machine-port";
import { h, prefs } from "../ui/dom";
import { Breakpoints } from "./breakpoints";
import { breakpointsView } from "./breakpoints-view";
import { registersView } from "./registers";
import { TRACE_ROWS, traceView } from "./trace";

type Tab = "cpu" | "trace" | "bps";
const TABS: { id: Tab | "mem" | "dev"; text: string; later?: boolean }[] = [
  { id: "cpu", text: "CPU" },
  { id: "trace", text: "Bus trace" },
  { id: "bps", text: "Breakpoints" },
  { id: "mem", text: "Memory", later: true },
  { id: "dev", text: "Devices", later: true },
];

const TRACE_DEPTH = 1024;
const POLL_MS = 250;

export function debuggerPane(port: MachinePort) {
  const bps = new Breakpoints(port);
  const regs = registersView();
  const trace = traceView();
  const bpv = breakpointsView(bps);
  const sections: Record<Tab, HTMLElement> = { cpu: regs.element, trace: trace.element, bps: bpv.element };

  let open = prefs.get("dbg") !== "closed";
  let tab: Tab = (["cpu", "trace", "bps"] as Tab[]).find((t) => t === prefs.get("dbg.tab")) ?? "cpu";
  let connected = false;

  const tabButtons = TABS.map((t) => {
    const b = h(
      "button",
      {
        role: "tab",
        type: "button",
        "data-tab": t.id,
        disabled: !!t.later,
        title: t.later ? "Coming in a later step" : undefined,
      },
      t.text,
    );
    if (!t.later) b.addEventListener("click", () => select(t.id as Tab));
    return b;
  });
  const body = h("div", { class: "dbg-b", role: "tabpanel" });
  const element = h(
    "aside",
    { class: "dbg", "aria-label": "Debugger", "data-testid": "debugger" },
    h("div", { class: "dbg-h" }, h("div", { class: "tabs", role: "tablist" }, ...tabButtons)),
    body,
  );

  const toggle = h("button", { class: "tbtn", type: "button", title: "Show or hide the debugger", "data-testid": "tb-debugger" }, "Debugger ◧");
  const reopen = h("button", { class: "reopen", type: "button", "data-testid": "dbg-reopen" }, "◀ DEBUGGER");
  toggle.addEventListener("click", () => setOpen(!open));
  reopen.addEventListener("click", () => setOpen(true));

  /** Watch what the open tab shows, and nothing else. */
  const watch = () => {
    if (!connected) return;
    port.watchRegisters(open && tab === "cpu");
    port.setTrace(open && tab === "trace", TRACE_DEPTH);
  };

  function select(t: Tab): void {
    tab = t;
    prefs.set("dbg.tab", t);
    tabButtons.forEach((b) => b.setAttribute("aria-selected", String(b.dataset.tab === t)));
    body.replaceChildren(sections[t]);
    watch();
    polledAt = -1; // fresh now, not at the next poll
  }

  function setOpen(o: boolean): void {
    open = o;
    prefs.set("dbg", o ? "open" : "closed");
    document.body.classList.toggle("dbg-closed", !o);
    toggle.classList.toggle("on", o);
    toggle.setAttribute("aria-pressed", String(o));
    watch();
    // the monitor changes size with the pane; the CRT follows through its ResizeObserver
  }

  let lastPoll = 0;
  let polledAt = -1; // the cycle count at the last poll
  let wasPowered = true;
  let polling = false;
  const poll = async () => {
    if (polling) return;
    polling = true;
    try {
      if (tab === "trace") trace.show(await port.readTrace(TRACE_ROWS));
      bpv.showHit(await bps.hit());
    } finally {
      polling = false;
    }
  };

  const update = (f: FrameState) => {
    if (f.powered && !wasPowered) bps.write(); // the card forgot at power off: arm it again
    wasPowered = f.powered;
    if (!open) return;
    if (tab === "cpu") regs.update(f);
    // Running: a few times a second is plenty. Halted: once per step (the
    // cycle count moved), so the trace and the hit follow CONT exactly.
    const now = performance.now();
    if (f.powered && f.cycles !== polledAt && (f.halted || now - lastPoll >= POLL_MS || polledAt < 0)) {
      lastPoll = now;
      polledAt = f.cycles;
      void poll();
    }
  };

  /** After the port connects: start watching, and arm nothing (the card starts clear). */
  const connect = () => {
    connected = true;
    bps.arm(null);
    watch();
  };

  select(tab);
  setOpen(open);
  return { element, toggle, reopen, update, connect, breakpoints: bps };
}
