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
 * ANSI's cursor and erase controls, as OODOS sends them on the serial line
 * (its VT52 screen codes, oodos/sys/bios.s's TTYOUT): ESC [ n A, B, C, D
 * move the cursor n (1 if none) up, down, right, left, stopping at the
 * edges; ESC [ row ; col H (or f) moves to that row and column, from 1, past
 * the edge the last; ESC [ J and ESC [ K erase to the end of the screen and
 * of the line, 1 to their start, 2 all of it, the cursor staying. Any other
 * ESC [ sequence is dropped, to its final byte; an ESC not followed by [ is
 * ignored and the byte after it treated as ever (the Woz Monitor echoes ESC
 * before its backslash).
 *
 * The renderer redraws only what changed: `dirty` holds the rows touched
 * since takeDirty().
 */

export const COLS = 80;
export const ROWS = 30;

const CR = 0x0d;
const LF = 0x0a;
const BS = 0x08;
const ESC = 0x1b;

export class Terminal {
  readonly cells = new Uint8Array(COLS * ROWS).fill(0x20);
  col = 0;
  row = 0;
  private afterCr = false;
  /** 0, 1 after ESC, 2 in an ESC [ sequence (its parameters in `params`). */
  private esc = 0;
  private params = "";
  private dirty = new Set<number>();
  /** Bumped on every scroll, so a renderer can redraw the whole screen. */
  scrolls = 0;

  write(bytes: Uint8Array): void {
    for (const raw of bytes) this.put(raw & 0x7f);
  }

  private put(c: number): void {
    if (this.esc === 1) {
      this.esc = 0;
      if (c === 0x5b) {
        this.esc = 2;
        this.params = "";
        return;
      }
    } else if (this.esc === 2) {
      if (c >= 0x40 && c <= 0x7e) {
        this.esc = 0;
        this.control(c, this.params);
      } else if (c >= 0x20) {
        this.params += String.fromCharCode(c);
      }
      return;
    }
    if (c === ESC) {
      this.esc = 1;
      this.afterCr = false;
      return;
    }
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

  /** An ESC [ sequence's final byte `f`, with its parameters. */
  private control(f: number, params: string): void {
    const n = params.split(";").map((p) => (/^\d+$/.test(p) ? Number(p) : 0));
    const count = Math.max(1, n[0] ?? 0);
    const col = Math.min(this.col, COLS - 1);
    switch (String.fromCharCode(f)) {
      case "A":
        this.moveTo(this.row - count, col);
        break;
      case "B":
        this.moveTo(this.row + count, col);
        break;
      case "C":
        this.moveTo(this.row, col + count);
        break;
      case "D":
        this.moveTo(this.row, col - count);
        break;
      case "H":
      case "f":
        this.moveTo(Math.max(1, n[0] ?? 0) - 1, Math.max(1, n[1] ?? 0) - 1);
        break;
      case "J":
        this.erase(n[0] ?? 0, this.row * COLS + col, 0, ROWS * COLS);
        break;
      case "K":
        this.erase(n[0] ?? 0, this.row * COLS + col, this.row * COLS, (this.row + 1) * COLS);
        break;
    }
  }

  private moveTo(row: number, col: number): void {
    this.touch(this.row);
    this.row = Math.min(Math.max(row, 0), ROWS - 1);
    this.col = Math.min(Math.max(col, 0), COLS - 1);
    this.touch(this.row);
  }

  /** Erase from the cursor `at` to `end` (mode 0), from `start` through it (1), or all of start-end (2). */
  private erase(mode: number, at: number, start: number, end: number): void {
    if (mode > 2) return;
    const [from, to] = mode === 1 ? [start, at + 1] : mode === 2 ? [start, end] : [at, end];
    this.cells.fill(0x20, from, to);
    for (let r = Math.floor(from / COLS); r <= Math.floor((to - 1) / COLS); ++r) this.touch(r);
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
    this.esc = 0;
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
