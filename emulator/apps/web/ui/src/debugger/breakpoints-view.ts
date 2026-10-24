/**
 * The Breakpoints tab: add by label or address, arm one (the card has one
 * comparator), choose what it matches (fetch, read, write) and what it does
 * (halt, or /NMI for a debugger on the machine), and see the cycle it
 * caught: LADDR, LDATA and LSTAT, read from the card when STATUS says hit.
 */

import { h } from "../ui/dom";
import { Breakpoints, describe, hex, type Flag, type Hit } from "./breakpoints";

function statText(stat: number): string {
  if (stat & 0x01) return "fetch";
  if (stat & 0x04) return "vector read";
  return stat & 0x02 ? "read" : "write";
}

export function breakpointsView(bps: Breakpoints) {
  const input = h("input", {
    type: "text",
    placeholder: "ECHO, C954 or FC954",
    "aria-label": "Breakpoint address or label",
    spellcheck: "false",
    autocomplete: "off",
    "data-testid": "bp-input",
  });
  const add = h("button", { class: "tbtn", type: "submit", "data-testid": "bp-add" }, "Add");
  const error = h("p", { class: "bp-error", role: "alert", "data-testid": "bp-error" });
  const form = h("form", { class: "bp-add" }, input, add);
  form.addEventListener("submit", (e) => {
    e.preventDefault();
    void bps.add(input.value).then((r) => {
      if (typeof r === "string") {
        error.textContent = r;
      } else {
        error.textContent = "";
        input.value = "";
        if (bps.armed === null) bps.arm(r.id); // the first one is armed, like a fresh comparator
      }
    });
  });

  const list = h("div", { class: "bps", role: "radiogroup", "aria-label": "Breakpoints: one armed at a time" });
  const none = h("label", { class: "bp-row none" });
  const hitBox = h("div", { class: "bp-hit", "data-testid": "bp-hit", hidden: true });

  const render = () => {
    const rows = bps.list.map((b) => {
      const armed = bps.armed === b.id;
      const radio = h("input", { type: "radio", name: "bp-armed", "aria-label": `Arm ${describe(b)}` });
      radio.checked = armed;
      radio.addEventListener("change", () => bps.arm(b.id));
      const flag = (f: Flag, text: string) => {
        const box = h("input", { type: "checkbox", "data-flag": f });
        box.checked = b[f];
        box.addEventListener("change", () => bps.setFlag(b.id, f, box.checked));
        return h("label", { class: "bp-flag" }, box, text);
      };
      const del = h("button", { class: "bp-del", type: "button", "aria-label": `Delete ${describe(b)}` }, "×");
      del.addEventListener("click", () => bps.remove(b.id));
      return h(
        "div",
        { class: `bp-row${armed ? "" : " off"}`, "data-testid": "bp-row", "data-armed": String(armed) },
        h("label", { class: "bp-arm" }, radio, h("span", { class: "dot" }), h("span", { class: "bp-what" }, describe(b))),
        h(
          "span",
          { class: "bp-flags" },
          flag("fetch", "fetch"),
          flag("read", "read"),
          flag("write", "write"),
          flag("nmi", "→ NMI"),
        ),
        h("small", {}, armed ? (b.nmi ? "armed → /NMI" : "armed → halt") : "saved"),
        del,
      );
    });
    const off = h("input", { type: "radio", name: "bp-armed", "aria-label": "None armed" });
    off.checked = bps.armed === null;
    off.addEventListener("change", () => bps.arm(null));
    none.replaceChildren(off, h("span", {}, "none armed"));
    list.replaceChildren(...rows, ...(bps.list.length ? [none] : []));
  };
  bps.onChange(render);
  render();

  const clear = h("button", { class: "tbtn", type: "button", "data-testid": "bp-clear" }, "Clear");
  clear.addEventListener("click", () => {
    bps.clearHit();
    showHit(null);
  });
  const hitText = h("span", { "data-testid": "bp-hit-text" });
  hitBox.append(hitText, clear);

  const showHit = (hit: Hit | null) => {
    hitBox.hidden = hit === null;
    if (hit) hitText.textContent = `hit: ${hex(hit.physical, 5)}  data ${hex(hit.data, 2)}  ${statText(hit.stat)}`;
  };

  const element = h(
    "div",
    { class: "sec" },
    h("h4", {}, "Breakpoints ", h("em", {}, "Console card BPADDR / BPCTRL")),
    form,
    error,
    list,
    hitBox,
    h(
      "p",
      { class: "hint" },
      "One comparator, so one armed. A fetch halts before the instruction; a read or write is caught on its cycle and halts after the instruction. CONT (or Run) goes on.",
    ),
  );
  return { element, showHit };
}
