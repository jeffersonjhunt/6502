/** A small element builder: h("button", { class: "tbtn", title: "Run" }, "Run"). */
export function h<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs: Record<string, string | boolean | undefined> = {},
  ...children: (Node | string)[]
): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === false) continue;
    e.setAttribute(k, v === true ? "" : v);
  }
  e.append(...children);
  return e;
}

/** A row of buttons where one is pressed (aria-pressed), like a rotary switch. */
export function segmented<T extends string>(
  label: string,
  options: { value: T; text: string; swatch?: string; title?: string }[],
  selected: T,
  onPick: (value: T) => void,
): { element: HTMLElement; select: (value: T) => void } {
  const group = h("div", { class: "seg", role: "group", "aria-label": label });
  const buttons = options.map((o) => {
    const b = h("button", { type: "button", "data-value": o.value, title: o.title });
    if (o.swatch) b.append(h("i", { class: "sw8", style: `background:${o.swatch}` }));
    b.append(o.text);
    b.addEventListener("click", () => {
      select(o.value);
      onPick(o.value);
    });
    group.append(b);
    return b;
  });
  const select = (value: T) => buttons.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.value === value)));
  select(selected);
  return { element: group, select };
}

/** localStorage that never throws (private windows, blocked storage). */
export const prefs = {
  get(key: string): string | null {
    try {
      return localStorage.getItem(`oo1.${key}`);
    } catch {
      return null;
    }
  },
  set(key: string, value: string): void {
    try {
      localStorage.setItem(`oo1.${key}`, value);
    } catch {
      /* not remembered: fine */
    }
  },
  remove(key: string): void {
    try {
      localStorage.removeItem(`oo1.${key}`);
    } catch {
      /* nothing kept: fine */
    }
  },
};

/** A button hid (`from`, picked or pressed): focus its counterpart, if focus was on it or has dropped to the page. */
export function passFocus(from: HTMLElement, to: HTMLElement): void {
  const at = document.activeElement;
  if (!at || at === from || at === document.body) to.focus();
}

const TAB_STOPS = 'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]';

/**
 * Focus the tab stop after `from` (way 1) or before it (-1), as Tab would:
 * not one Tab skips (tabindex=-1, inert, not laid out), and past one that
 * won't take focus (visibility: hidden) to the next. @returns false if none did.
 */
export function focusStop(from: HTMLElement, way: 1 | -1): boolean {
  const stops = Array.from(document.querySelectorAll<HTMLElement>(TAB_STOPS)).filter(
    (el) => el === from || (el.tabIndex >= 0 && !el.closest("[inert]") && el.getClientRects().length > 0),
  );
  const at = stops.indexOf(from);
  if (at < 0) return false;
  for (let i = at + way; i >= 0 && i < stops.length; i += way) {
    stops[i].focus();
    if (document.activeElement === stops[i]) return true;
  }
  return false;
}
