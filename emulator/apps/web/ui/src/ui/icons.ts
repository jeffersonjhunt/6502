/**
 * The toolbar's and the strip's little line icons (the mockups'), as
 * inline SVG: decoration only, so hidden from screen readers; a button's
 * name is its text.
 */

const PATHS = {
  back: '<path d="M15 6l-6 6 6 6" fill="none" stroke="currentColor" stroke-width="2"/>',
  run: '<path d="M6 4l14 8-14 8z" fill="currentColor"/>',
  halt: '<path d="M6 4h4v16H6zM14 4h4v16h-4z" fill="currentColor"/>',
  sound: '<path d="M4 9h4l5-4v14l-5-4H4z" fill="none" stroke="currentColor" stroke-width="2"/><path d="M16.5 8.5a5 5 0 0 1 0 7" fill="none" stroke="currentColor" stroke-width="2"/>',
  settings:
    '<circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1" fill="none" stroke="currentColor" stroke-width="2"/>',
  debugger: '<rect x="3" y="4" width="18" height="16" rx="2" fill="none" stroke="currentColor" stroke-width="2"/><path d="M15 4v16" fill="none" stroke="currentColor" stroke-width="2"/>',
  more: '<circle cx="5" cy="12" r="2" fill="currentColor"/><circle cx="12" cy="12" r="2" fill="currentColor"/><circle cx="19" cy="12" r="2" fill="currentColor"/>',
  close: '<path d="M6 6l12 12M18 6L6 18" fill="none" stroke="currentColor" stroke-width="2"/>',
  note: '<path d="M9 18V5l12-2v13" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="6" cy="18" r="3" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="18" cy="16" r="3" fill="none" stroke="currentColor" stroke-width="2"/>',
  prev: '<path d="M18 4L8 12l10 8zM5 4h3v16H5z" fill="currentColor"/>',
  next: '<path d="M6 4l10 8-10 8zM16 4h3v16h-3z" fill="currentColor"/>',
  stop: '<path d="M5 5h14v14H5z" fill="currentColor"/>',
} as const;

export type IconName = keyof typeof PATHS;

export function icon(name: IconName): SVGSVGElement {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("class", `ico ico-${name}`);
  svg.innerHTML = PATHS[name];
  return svg;
}
