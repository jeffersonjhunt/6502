/**
 * The Bus trace tab: the latest bus cycles in the bus spy's terms
 * (docs/BUS_TRACE.md): cycle, the physical address after the north bridge,
 * the CPU's address, data, read or write, and SYNC / VPB. I/O-hole cycles
 * are tinted and name their module; fetches are bright. The same cycles the
 * panel's lamps average.
 */

import type { TraceRow } from "../port/machine-port";
import { h } from "../ui/dom";
import { hex } from "./breakpoints";

export const TRACE_ROWS = 64;

export function isIo(row: TraceRow): boolean {
  return row.cpu >= 0xc000 && row.cpu < 0xc800;
}

export function traceView() {
  const body = h("tbody", { "data-testid": "trace-rows" });
  const scroller = h(
    "div",
    { class: "trace" },
    h(
      "table",
      {},
      h(
        "thead",
        {},
        h("tr", {}, ...["cycle", "phys", "cpu", "data", "r/w", "sig"].map((t) => h("th", {}, t))),
      ),
      body,
    ),
  );
  const element = h(
    "div",
    { class: "sec" },
    h("h4", {}, "Bus trace ", h("em", {}, "newest last; recorded only while this tab is open")),
    scroller,
  );

  const show = (rows: TraceRow[]) => {
    const atBottom = scroller.scrollTop + scroller.clientHeight >= scroller.scrollHeight - 4;
    body.replaceChildren(
      ...rows.map((r, i) => {
        const sig = [r.sync ? "SYNC" : "", r.vpb ? "VPB" : "", isIo(r) ? `I/O m${r.module.toString(16).toUpperCase()}` : ""]
          .filter(Boolean)
          .join(" ");
        const cls = [r.sync ? "f" : "", isIo(r) ? "io" : "", i === rows.length - 1 ? "last" : ""].filter(Boolean).join(" ");
        return h(
          "tr",
          { class: cls || undefined },
          h("td", {}, String(r.cycle)),
          h("td", {}, hex(r.physical, 5)),
          h("td", {}, hex(r.cpu, 4)),
          h("td", {}, hex(r.data, 2)),
          h("td", {}, r.write ? "W" : "R"),
          h("td", { class: r.sync ? "s" : undefined }, sig),
        );
      }),
    );
    if (atBottom) scroller.scrollTop = scroller.scrollHeight;
  };
  return { element, show };
}
