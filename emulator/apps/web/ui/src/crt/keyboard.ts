/**
 * Keys and pastes to the bytes a serial terminal sends. Enter is CR (the
 * Woz Monitor ends a line on CR), Backspace is BS, Escape ESC, Ctrl+letter
 * its control code. With CAPS on (the default: Woz only knows capitals)
 * letters go up in case. Keys with Alt or the platform key are left to the
 * browser, so its shortcuts keep working.
 */

export interface KeyLike {
  key: string;
  ctrlKey: boolean;
  altKey: boolean;
  metaKey: boolean;
}

const NAMED: Record<string, number> = { Enter: 0x0d, Backspace: 0x08, Escape: 0x1b, Tab: 0x09 };

/** @returns the bytes to send, or null for a key the terminal does not take */
export function keyBytes(e: KeyLike, caps: boolean): Uint8Array | null {
  if (e.altKey || e.metaKey) return null;
  if (e.key in NAMED) return Uint8Array.of(NAMED[e.key]);
  if (e.key.length !== 1) return null;
  let c = e.key.charCodeAt(0);
  if (c >= 0x80) return null;
  if (e.ctrlKey) {
    const upper = e.key.toUpperCase().charCodeAt(0);
    return upper >= 0x40 && upper <= 0x5f ? Uint8Array.of(upper & 0x1f) : null;
  }
  if (caps && c >= 0x61 && c <= 0x7a) c -= 0x20;
  return Uint8Array.of(c);
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
