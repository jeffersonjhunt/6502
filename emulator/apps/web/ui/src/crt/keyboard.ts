/**
 * Keys and pastes to the bytes a serial terminal sends. Enter is CR (the
 * Woz Monitor ends a line on CR), Backspace is BS, Escape ESC, Ctrl+letter
 * its control code. With CAPS on (the default: Woz only knows capitals)
 * letters go up in case. Keys with Alt or the platform key are left to the
 * browser, so its shortcuts keep working; but a character AltGr makes (@ [ ]
 * on a German keyboard), or Option on the Mac, is typed. The paste chords
 * are the browser's too (isPaste), and F6 leaves the screen (leavesScreen):
 * Tab and Escape are the machine's.
 */

export interface KeyLike {
  key: string;
  code?: string;
  ctrlKey: boolean;
  altKey: boolean;
  metaKey: boolean;
  shiftKey?: boolean;
  getModifierState?(key: string): boolean;
}

const NAMED: Record<string, number> = { Enter: 0x0d, Backspace: 0x08, Escape: 0x1b, Tab: 0x09 };

/** On the Mac, Cmd is the browser's and Option makes characters; Ctrl+V is no paste there. */
export const IS_MAC = typeof navigator !== "undefined" && /Mac|iPhone|iPad|iPod/.test(navigator.platform ?? "");

/** @returns the bytes to send, or null for a key the terminal does not take */
export function keyBytes(e: KeyLike, caps: boolean, mac = IS_MAC): Uint8Array | null {
  const altGraph = e.getModifierState?.("AltGraph") ?? false; // Windows also sets Ctrl and Alt for it
  if (e.metaKey || (e.altKey && !altGraph && !mac)) return null;
  if (e.key in NAMED) return Uint8Array.of(NAMED[e.key]);
  if (e.key.length !== 1) return null;
  let c = e.key.charCodeAt(0);
  if (c >= 0x80) return null;
  if (e.ctrlKey && !altGraph) {
    const upper = e.key.toUpperCase().charCodeAt(0);
    return upper >= 0x40 && upper <= 0x5f ? Uint8Array.of(upper & 0x1f) : null;
  }
  if (caps && c >= 0x61 && c <= 0x7a) c -= 0x20;
  return Uint8Array.of(c);
}

// A US keyboard's digits and punctuation, by KeyboardEvent.code: each without Shift and with it.
const US_CODES = [
  "Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "Digit0", "Minus", "Equal",
  "BracketLeft", "BracketRight", "Backslash", "Semicolon", "Quote", "Backquote", "Comma", "Period", "Slash",
];
const US_PLAIN = "1234567890-=[]\\;'`,./";
const US_SHIFTED = '!@#$%^&*()_+{}|:"~<>?';

/**
 * The key as it is without Shift, for when a Shift that is down isn't the
 * typist's (the left Shift held as a joystick's fire): a letter in lower
 * case; a digit or punctuation what its key makes unshifted, where the
 * event is what Shift makes on that key on a US keyboard (another layout's
 * is left as it came: there is no knowing its unshifted character).
 */
export function unshifted(e: KeyLike & { code: string }): KeyLike & { code: string; shiftKey: false } {
  let key = e.key;
  if (key.length === 1 && key.toLowerCase() !== key) key = key.toLowerCase();
  else {
    const i = US_CODES.indexOf(e.code);
    if (i >= 0 && US_SHIFTED[i] === key) key = US_PLAIN[i];
  }
  // Fields by name: a KeyboardEvent's are getters on its prototype, which a spread would miss.
  return {
    key,
    code: e.code,
    ctrlKey: e.ctrlKey,
    altKey: e.altKey,
    metaKey: e.metaKey,
    shiftKey: false,
    getModifierState: (m: string) => m !== "Shift" && (e.getModifierState?.(m) ?? false),
  };
}

/**
 * Ctrl+V, Ctrl+Shift+V or Shift+Insert: the browser's paste, so the page gets the text (Cmd+V on the Mac is anyway).
 * The V key's code only where the layout's letter isn't Latin (Russian): on Dvorak it is K, and Ctrl+K is the machine's.
 */
export function isPaste(e: KeyLike, mac = IS_MAC): boolean {
  if (e.altKey || e.metaKey) return false;
  if (e.key === "Insert") return !!e.shiftKey && !e.ctrlKey;
  const k = e.key.toLowerCase();
  return !mac && e.ctrlKey && (k === "v" || (e.code === "KeyV" && !/^[a-z]$/.test(k)));
}

/**
 * F6 takes the keyboard off the screen (1: to what comes after it, -1:
 * Shift+F6, before), as F6 moves between a browser's panes. Neither machine
 * makes anything of F6, and Tab and Escape are theirs.
 */
export function leavesScreen(e: KeyLike): -1 | 0 | 1 {
  if (e.key !== "F6" || e.ctrlKey || e.altKey || e.metaKey) return 0;
  return e.shiftKey ? -1 : 1;
}

/** Pasted text: line ends become CR, other non-ASCII is dropped. */
export function pasteBytes(text: string, caps: boolean): Uint8Array {
  const out: number[] = [];
  const s = text.replace(/\r\n?|\n/g, "\r");
  for (const ch of caps ? s.toUpperCase() : s) {
    const c = ch.charCodeAt(0);
    if (c === 0x0d || c === 0x09 || (c >= 0x20 && c < 0x7f)) out.push(c);
  }
  return Uint8Array.from(out);
}
