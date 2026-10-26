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

/** Ctrl+V, Ctrl+Shift+V or Shift+Insert: the browser's paste, so the page gets the text (Cmd+V on the Mac is anyway). */
export function isPaste(e: KeyLike, mac = IS_MAC): boolean {
  if (e.altKey || e.metaKey) return false;
  if (e.key === "Insert") return !!e.shiftKey && !e.ctrlKey;
  return !mac && e.ctrlKey && (e.code === "KeyV" || e.key.toLowerCase() === "v");
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
