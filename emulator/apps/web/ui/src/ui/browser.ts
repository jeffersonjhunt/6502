/**
 * Which browser engine runs the page, for the note that says the emulator works best in Chrome: it is
 * tested in full on Chromium, and on WebKit (Safari) as a best effort.
 */

/** The parts of `navigator` this reads. */
export interface Nav {
  userAgent: string;
  userAgentData?: { brands?: { brand: string }[] };
}

/**
 * Whether the page runs on Chromium (Chrome, Edge, and others built on it). Chromium names itself in
 * `userAgentData`; where that is missing, its user agent says `Chrome/`. On iOS every browser is WebKit,
 * whatever its name, so iOS's Chrome (`CriOS`) and Edge (`EdgiOS`) are not Chromium.
 */
export function chromium(nav: Nav): boolean {
  if (nav.userAgentData?.brands?.some((b) => b.brand === "Chromium")) return true;
  return /\bChrome\//.test(nav.userAgent) && !/\b(CriOS|EdgiOS|FxiOS)\//.test(nav.userAgent);
}

export const BEST_IN = "The OO-1 emulator works best in Chrome or Edge. In other browsers, Safari among them, it runs on a best-effort basis.";
