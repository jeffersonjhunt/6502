/**
 * The USB keyboard: browser key events as USB HID usage codes, for the USB
 * card (hw/design.md, "USB card registers"), which cooks them itself in its
 * own layout. KeyboardEvent.code names the physical key whatever the host's
 * layout, which is what a USB keyboard sends.
 *
 * Keys still held when the page loses focus are released, or the machine
 * would see them down for ever. Combinations with the Mac's Command key are
 * left to the browser (Cmd-R, Cmd-T...): the key-up for them can go missing.
 * Shift, Ctrl and Alt are kept in step with each event's own flags, so a
 * modifier event that never came (focus changes; some synthetic input)
 * cannot make the wrong character.
 */

const USAGES: Record<string, number> = {
  Enter: 0x28, Escape: 0x29, Backspace: 0x2a, Tab: 0x2b, Space: 0x2c, Minus: 0x2d, Equal: 0x2e,
  BracketLeft: 0x2f, BracketRight: 0x30, Backslash: 0x31, Semicolon: 0x33, Quote: 0x34, Backquote: 0x35,
  Comma: 0x36, Period: 0x37, Slash: 0x38, CapsLock: 0x39, PrintScreen: 0x46, ScrollLock: 0x47, Pause: 0x48,
  Insert: 0x49, Home: 0x4a, PageUp: 0x4b, Delete: 0x4c, End: 0x4d, PageDown: 0x4e, ArrowRight: 0x4f,
  ArrowLeft: 0x50, ArrowDown: 0x51, ArrowUp: 0x52, NumLock: 0x53, NumpadDivide: 0x54, NumpadMultiply: 0x55,
  NumpadSubtract: 0x56, NumpadAdd: 0x57, NumpadEnter: 0x58, NumpadDecimal: 0x63, IntlBackslash: 0x64,
  ContextMenu: 0x65, ControlLeft: 0xe0, ShiftLeft: 0xe1, AltLeft: 0xe2, MetaLeft: 0xe3, ControlRight: 0xe4,
  ShiftRight: 0xe5, AltRight: 0xe6, MetaRight: 0xe7,
};

/** The HID usage for a KeyboardEvent.code, or null for a key the card has no usage for. */
export function usageFor(code: string): number | null {
  let m = /^Key([A-Z])$/.exec(code);
  if (m) return 0x04 + m[1].charCodeAt(0) - 65;
  m = /^Digit([0-9])$/.exec(code);
  if (m) return m[1] === "0" ? 0x27 : 0x1e + Number(m[1]) - 1;
  m = /^Numpad([0-9])$/.exec(code);
  if (m) return m[1] === "0" ? 0x62 : 0x59 + Number(m[1]) - 1;
  m = /^F([0-9]{1,2})$/.exec(code);
  if (m && Number(m[1]) >= 1 && Number(m[1]) <= 12) return 0x3a + Number(m[1]) - 1;
  return USAGES[code] ?? null;
}

const SHIFTED = '!@#$%^&*()_+{}|:"~<>?';
const UNSHIFTED = "1234567890-=[]\\;'`,./";
const SHIFT = 0xe1;

/**
 * Text as a US keyboard types it (the card's own layout): each character's
 * key, with Shift where it needs it. Letters go without Shift, so they
 * come out as the card's Caps Lock has them: capitals from power-up, as
 * Applesoft and the Woz Monitor want (with Shift they would be the other
 * case). Line ends (CR, LF or both) are Return; a character with no key on
 * it is left out.
 */
export function keysFor(text: string): { usage: number; shift: boolean }[] {
  const keys: { usage: number; shift: boolean }[] = [];
  for (const ch of text.replace(/\r\n?/g, "\n")) {
    const code = (c: string) => usageFor(/[a-z]/i.test(c) ? `Key${c.toUpperCase()}` : /[0-9]/.test(c) ? `Digit${c}` : PUNCTUATION[c] ?? "");
    if (ch === "\n") keys.push({ usage: 0x28, shift: false });
    else if (ch === "\t") keys.push({ usage: 0x2b, shift: false });
    else if (ch === " ") keys.push({ usage: 0x2c, shift: false });
    else if (SHIFTED.includes(ch)) keys.push({ usage: code(UNSHIFTED[SHIFTED.indexOf(ch)])!, shift: true });
    else {
      const usage = code(ch);
      if (usage !== null) keys.push({ usage, shift: false });
    }
  }
  return keys;
}

const PUNCTUATION: Record<string, string> = {
  "-": "Minus", "=": "Equal", "[": "BracketLeft", "]": "BracketRight", "\\": "Backslash", ";": "Semicolon",
  "'": "Quote", "`": "Backquote", ",": "Comma", ".": "Period", "/": "Slash",
};

/** Type text on the USB keyboard: each key pressed and let go, Shift around those that need it. */
export function typeText(text: string, send: (usage: number, down: boolean) => void): void {
  for (const { usage, shift } of keysFor(text)) {
    if (shift) send(SHIFT, true);
    send(usage, true);
    send(usage, false);
    if (shift) send(SHIFT, false);
  }
}

export class UsbKeyboard {
  private readonly held = new Set<number>();

  constructor(private readonly send: (usage: number, down: boolean) => void) {}

  /** Press or release a modifier to match a flag: left if neither side is held. */
  private sync(on: boolean, left: number, right: number): void {
    const held = this.held.has(left) || this.held.has(right);
    if (on && !held) {
      this.held.add(left);
      this.send(left, true);
    } else if (!on && held) {
      for (const u of [left, right])
        if (this.held.delete(u)) this.send(u, false);
    }
  }

  /** @returns true if the event went to the machine (the page should not act on it too) */
  down(e: KeyboardEvent): boolean {
    if (e.metaKey && e.code !== "MetaLeft" && e.code !== "MetaRight") return false; // the browser's shortcut
    const usage = usageFor(e.code);
    if (usage === null) return false;
    if (usage < 0xe0) {
      this.sync(!!e.shiftKey, 0xe1, 0xe5);
      this.sync(!!e.ctrlKey, 0xe0, 0xe4);
      this.sync(!!e.altKey, 0xe2, 0xe6);
    }
    if (!this.held.has(usage)) {
      this.held.add(usage);
      this.send(usage, true);
    }
    return true; // auto-repeat is the card's job: repeats from the host are swallowed
  }

  up(e: KeyboardEvent): boolean {
    const usage = usageFor(e.code);
    if (usage === null || !this.held.has(usage)) return false;
    this.held.delete(usage);
    this.send(usage, false);
    return true;
  }

  /** Focus went elsewhere: let go of everything. */
  releaseAll(): void {
    for (const usage of this.held) this.send(usage, false);
    this.held.clear();
  }
}
