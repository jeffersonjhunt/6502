/**
 * The strip between the monitor and the front panel: the power light,
 * "OO-1 · <signal> · RUNNING · 7.16 MHz", then pills for what is loaded
 * (the drives and the SD card, with their lights; each opens Settings at
 * its row), the tune playing, Stop typing while a paste types, and in
 * Apple II mode the IIe's Reset and Reboot. With nothing loaded it says
 * so, with a way to Settings.
 */

import { h } from "./dom";

export interface StripParts {
  /** The media's pills, each hidden while it has nothing to show. */
  pills: HTMLElement[];
  /** At the far end: Stop typing, the IIe's resets. */
  trailing: HTMLElement[];
  /** "Load in Settings". */
  load: () => void;
}

export function statusStrip(parts: StripParts) {
  const led = h("span", { class: "led power on", "aria-hidden": "true", "data-testid": "power-led" });
  const signal = h("span", { class: "strip-signal" }, "VGA 640×480");
  const status = h("span", { class: "mon-status", "data-testid": "status" }, "STARTING");
  const load = h("button", { class: "linkish", type: "button", "data-testid": "load-in-settings" }, "Load in Settings");
  load.addEventListener("click", parts.load);
  const empty = h("span", { class: "strip-empty", "data-testid": "no-media" }, "No media loaded. ", load);
  const element = h(
    "section",
    { class: "strip", "aria-label": "Machine status and media", "data-testid": "strip" },
    led,
    h("span", { class: "strip-text" }, h("span", { class: "strip-what" }, "OO-1 · ", signal, " · "), status),
    ...parts.pills,
    empty,
    h("span", { class: "grow" }),
    ...parts.trailing,
  );

  let none = false;
  /** Once a frame: the light, and "No media loaded." when no pill shows. */
  const update = (powered: boolean) => {
    led.classList.toggle("on", powered);
    const nothing = parts.pills.every((p) => p.hidden);
    if (nothing !== none) empty.hidden = !(none = nothing);
  };
  empty.hidden = true;
  return { element, signal, status, update };
}
