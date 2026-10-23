/**
 * The serial terminal's screen: an 80x30 grid of characters (8x16 cells
 * fill the 640x480 CRT, as VGA text does) fed with the machine's output.
 *
 * Control bytes as the native app's terminal treats them: CR starts a new
 * line, and an LF straight after a CR is swallowed, so CR, LF and CRLF
 * endings all give one new line; a lone LF is a new line too. BS moves
 * left. Bit 7 is dropped (the Woz Monitor and the Apple-1 set it), and
 * anything else below space, or DEL, is ignored. Writing past column 80
 * wraps; a new line past row 30 scrolls.
 *
 * The renderer redraws only what changed: `dirty` holds the rows touched
 * since takeDirty().
 */

export const COLS = 80;
export const ROWS = 30;

const CR = 0x0d;
const LF = 0x0a;
const BS = 0x08;

export class Terminal {
  readonly cells = new Uint8Array(COLS * ROWS).fill(0x20);
  col = 0;
  row = 0;
  private afterCr = false;
  private dirty = new Set<number>();
  /** Bumped on every scroll, so a renderer can redraw the whole screen. */
  scrolls = 0;

  write(bytes: Uint8Array): void {
    for (const raw of bytes) this.put(raw & 0x7f);
  }

  private put(c: number): void {
    if (c === LF && this.afterCr) {
      this.afterCr = false;
      return;
    }
    this.afterCr = c === CR;
    if (c === CR || c === LF) {
      this.newline();
      return;
    }
    if (c === BS) {
      if (this.col > 0) --this.col;
      this.touch(this.row);
      return;
    }
    if (c < 0x20 || c === 0x7f) return;
    if (this.col >= COLS) this.newline();
    this.cells[this.row * COLS + this.col] = c;
    ++this.col;
    this.touch(this.row);
  }

  private newline(): void {
    this.touch(this.row);
    this.col = 0;
    if (this.row < ROWS - 1) {
      ++this.row;
    } else {
      this.cells.copyWithin(0, COLS);
      this.cells.fill(0x20, (ROWS - 1) * COLS);
      ++this.scrolls;
      for (let r = 0; r < ROWS; ++r) this.dirty.add(r);
    }
    this.touch(this.row);
  }

  private touch(row: number): void {
    this.dirty.add(row);
  }

  clear(): void {
    this.cells.fill(0x20);
    this.col = 0;
    this.row = 0;
    this.afterCr = false;
    for (let r = 0; r < ROWS; ++r) this.dirty.add(r);
  }

  /** Rows changed since the last call. */
  takeDirty(): number[] {
    const rows = [...this.dirty];
    this.dirty.clear();
    return rows;
  }

  line(row: number): string {
    return String.fromCharCode(...this.cells.subarray(row * COLS, (row + 1) * COLS)).trimEnd();
  }

  /** The screen as text, trailing blank lines dropped. */
  text(): string {
    const lines: string[] = [];
    for (let r = 0; r < ROWS; ++r) lines.push(this.line(r));
    while (lines.length > 1 && lines[lines.length - 1] === "") lines.pop();
    return lines.join("\n");
  }
}
