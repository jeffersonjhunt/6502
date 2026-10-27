/**
 * The one right-hand drawer, shared by Settings and the Debugger: opening
 * one closes the other, and each toggles from its toolbar button (pressed
 * while it shows). Beside the bench on a wide window, over it on a narrow
 * one; on a phone it fills the screen and is a modal dialog, the page
 * behind it inert.
 *
 * Focus goes into it when it opens from a click (to `at`, or the panel),
 * and back to the button that opened it when it closes; Escape closes it
 * from inside. Whether the debugger was open is remembered (a first visit
 * starts with it closed); Settings always starts closed.
 */

import { h, prefs } from "./dom";
import { icon } from "./icons";

export type DrawerId = "settings" | "debugger";

export interface DrawerPanel {
  element: HTMLElement;
  /** Its toolbar toggle. */
  button: HTMLElement;
  /** Its name, for the close button and the region. */
  label: string;
  /** Told when it shows or hides (the debugger watches only while shown). */
  shown?: (open: boolean) => void;
}

export interface DrawerOptions {
  /** Where focus goes back to for a button (More, when the button is folded into its menu). */
  focusBack?: (button: HTMLElement) => HTMLElement;
  /** A phone: the drawer is a modal dialog over the page. */
  phone?: MediaQueryList | null;
  /** Made inert while it is a modal dialog. */
  behind?: HTMLElement[];
}

export function drawer(panels: Record<DrawerId, DrawerPanel>, options: DrawerOptions = {}) {
  const element = h("div", { class: "drawer", hidden: true, "data-testid": "drawer" });
  let current: DrawerId | null = null;
  let opener: HTMLElement | null = null;

  const closers = {} as Record<DrawerId, HTMLButtonElement>;
  for (const id of ["settings", "debugger"] as DrawerId[]) {
    const p = panels[id];
    p.element.hidden = true;
    p.element.tabIndex = -1; // focus lands on the panel when nothing in it is asked for
    element.append(p.element);
    const close = h("button", { class: "tbtn icon close", type: "button", "aria-label": `Close ${p.label.toLowerCase()}`, title: "Close (Escape)", "data-testid": `${id}-close` }, icon("close"));
    close.addEventListener("click", () => hide());
    closers[id] = close;
    p.button.addEventListener("click", () => toggle(id, p.button));
  }

  /** Region beside the page; a modal dialog on a phone. */
  const roles = () => {
    const modal = !!options.phone?.matches;
    for (const id of ["settings", "debugger"] as DrawerId[]) {
      const e = panels[id].element;
      e.setAttribute("role", modal ? "dialog" : "region");
      if (modal) e.setAttribute("aria-modal", "true");
      else e.removeAttribute("aria-modal");
    }
    for (const b of options.behind ?? []) b.inert = modal && current !== null;
  };
  options.phone?.addEventListener("change", roles);

  const press = () => {
    for (const id of ["settings", "debugger"] as DrawerId[]) {
      const b = panels[id].button;
      b.setAttribute("aria-pressed", String(current === id));
      b.classList.toggle("on", current === id);
    }
  };

  /** Show `id`; `focus`: move focus into it (to `at` if given). `from`: the button focus goes back to. */
  function show(id: DrawerId, how: { focus?: boolean; at?: HTMLElement; from?: HTMLElement } = {}): void {
    if (current && current !== id) {
      panels[current].element.hidden = true;
      panels[current].shown?.(false);
    }
    const p = panels[id];
    const was = current;
    current = id;
    opener = how.from ?? p.button;
    p.element.hidden = false;
    element.hidden = false;
    document.body.classList.add("drawer-open");
    if (was !== id) p.shown?.(true);
    prefs.set("dbg", id === "debugger" ? "open" : "closed");
    press();
    roles();
    if (how.focus !== false) (how.at ?? p.element).focus({ preventScroll: true }); // the drawer scrolls on its own
  }

  function hide(how: { focus?: boolean } = {}): void {
    if (!current) return;
    const p = panels[current];
    p.element.hidden = true;
    p.shown?.(false);
    current = null;
    element.hidden = true;
    document.body.classList.remove("drawer-open");
    prefs.set("dbg", "closed");
    press();
    roles();
    if (how.focus !== false && opener) (options.focusBack?.(opener) ?? opener).focus();
  }

  function toggle(id: DrawerId, from?: HTMLElement): void {
    if (current === id) hide();
    else show(id, { from });
  }

  element.addEventListener("keydown", (e) => {
    if (e.key !== "Escape" || !current) return;
    e.preventDefault();
    e.stopPropagation();
    hide();
  });

  roles();
  press();
  /** The debugger, if it was left open last time: shown, focus left where it is. */
  const restore = () => {
    if (prefs.get("dbg") === "open") show("debugger", { focus: false });
  };
  return { element, show, hide, toggle, restore, closers, current: () => current };
}
