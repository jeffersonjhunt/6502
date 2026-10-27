/**
 * The front panel scaled down to the width there is (a phone's), rather
 * than cut off: the rack keeps its own layout, and a transform shrinks it
 * whole, so the panel itself is unchanged. Never scaled up.
 */

import { h } from "./dom";

/** The scale that fits `natural` pixels into `room`: at most 1. */
export function fitScale(natural: number, room: number): number {
  if (natural <= 0 || room <= 0) return 1;
  return Math.min(1, room / natural);
}

/** A box around `inner` that scales it to its own width and takes the scaled height. */
export function fitWidth(inner: HTMLElement): HTMLElement {
  const box = h("div", { class: "fit", "data-testid": "panel-fit" }, inner);
  const fit = () => {
    const s = fitScale(inner.offsetWidth, box.clientWidth);
    inner.style.transform = s < 1 ? `translateX(-50%) scale(${s.toFixed(4)})` : "translateX(-50%)";
    box.style.height = `${Math.ceil(inner.offsetHeight * s)}px`;
    box.dataset.scale = s.toFixed(3);
  };
  if (typeof ResizeObserver !== "undefined") {
    const watch = new ResizeObserver(fit);
    watch.observe(box);
    watch.observe(inner);
  }
  return box;
}
