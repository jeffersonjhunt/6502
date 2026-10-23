/**
 * The CRT: draws the terminal, or another scene, on the 640x480 canvas
 * inside the monitor's glass (the scanlines, vignette and glow are CSS
 * overlays, styles/crt.css).
 *
 * Text is drawn from a glyph atlas made once per phosphor colour (VT323,
 * squeezed to 8x16 cells, glow baked in), and only rows that changed are
 * redrawn, so an idle screen costs nothing per frame. Drawing is in VGA's
 * 640x480, but the canvas holds twice that, so the browser scales the
 * picture down to the glass rather than blurring it up.
 *
 * Scenes are the monitor's input select. "text" is the serial terminal.
 * "tiles" and "apple" are the video card's modes, which are not emulated
 * yet: they show NO SIGNAL, unless the page has ?demo=scenes, which draws
 * the mockup's still pictures for design review.
 */

import { COLS, ROWS, type Terminal } from "./terminal";
import { drawDemo } from "./scenes";

export type Scene = "text" | "tiles" | "apple";
export type Phosphor = "colour" | "p1" | "p3" | "p4";

export const PHOSPHORS: Record<Phosphor, { fg: string; rgb: [number, number, number] | null; halo: string }> = {
  colour: { fg: "#d6d6d6", rgb: null, halo: "#9bb7ff10" },
  p1: { fg: "#33ff66", rgb: [51, 255, 102], halo: "#33ff6614" },
  p3: { fg: "#ffb000", rgb: [255, 176, 0], halo: "#ffb00014" },
  p4: { fg: "#e8eef5", rgb: [232, 238, 245], halo: "#e8eef510" },
};

const W = 640;
const H = 480;
const CW = 8;
const CH = 16;
const FIRST = 0x20;
const GLYPHS = 0x7f - FIRST;
const BLINK_MS = 530;
const SCALE = 2; // backing pixels per VGA pixel

export class Crt {
  private readonly ctx: CanvasRenderingContext2D;
  private atlas: HTMLCanvasElement | null = null;
  private scene: Scene = "text";
  private phosphor: Phosphor = "p3";
  private powered = true;
  private full = true;
  private cursorOn = true;
  private cursorAt = { col: -1, row: -1 };
  private lastBlink = 0;
  private scrolls = -1;
  private readonly demo: boolean;
  private readonly blinks: boolean;

  constructor(
    canvas: HTMLCanvasElement,
    private readonly terminal: Terminal,
    options: { demo?: boolean; reducedMotion?: boolean } = {},
  ) {
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("no 2D canvas");
    this.ctx = ctx;
    canvas.width = W * SCALE;
    canvas.height = H * SCALE;
    ctx.setTransform(SCALE, 0, 0, SCALE, 0, 0);
    this.demo = options.demo ?? false;
    this.blinks = !options.reducedMotion;
  }

  setScene(scene: Scene): void {
    this.scene = scene;
    this.full = true;
  }

  setPhosphor(phosphor: Phosphor): void {
    this.phosphor = phosphor;
    this.atlas = null;
    this.full = true;
  }

  setPowered(on: boolean): void {
    if (on !== this.powered) this.full = true;
    this.powered = on;
  }

  /** The fonts arrived: rebuild the glyphs. */
  fontsReady(): void {
    this.atlas = null;
    this.full = true;
  }

  get currentScene(): Scene {
    return this.scene;
  }

  /** Called every animation frame. */
  draw(now: number): void {
    const ctx = this.ctx;
    if (!this.powered) {
      if (this.full) {
        ctx.fillStyle = "#000";
        ctx.fillRect(0, 0, W, H);
        this.full = false;
      }
      return;
    }
    if (this.scene !== "text") {
      if (this.full) {
        if (this.demo) drawDemo(ctx, this.scene, PHOSPHORS[this.phosphor].rgb);
        else this.noSignal();
        this.full = false;
      }
      return;
    }

    if (!this.atlas) {
      this.atlas = this.buildAtlas();
      this.full = true;
    }
    const rows = this.terminal.takeDirty();
    if (this.full || this.terminal.scrolls !== this.scrolls) {
      ctx.fillStyle = "#000";
      ctx.fillRect(0, 0, W, H);
      for (let r = 0; r < ROWS; ++r) this.drawRow(r);
      this.full = false;
      this.scrolls = this.terminal.scrolls;
      this.cursorAt = { col: -1, row: -1 };
    } else {
      for (const r of rows) this.drawRow(r);
    }

    if (this.blinks && now - this.lastBlink >= BLINK_MS) {
      this.cursorOn = !this.cursorOn;
      this.lastBlink = now;
    }
    const { col, row } = this.terminal;
    if (col !== this.cursorAt.col || row !== this.cursorAt.row) {
      if (this.cursorAt.row >= 0) this.drawRow(this.cursorAt.row); // rub out the old cursor
      this.cursorAt = { col, row };
    }
    this.drawCursor();
  }

  private drawRow(r: number): void {
    const ctx = this.ctx;
    const atlas = this.atlas!;
    ctx.fillStyle = "#000";
    ctx.fillRect(0, r * CH, W, CH);
    const cells = this.terminal.cells;
    for (let c = 0; c < COLS; ++c) {
      const ch = cells[r * COLS + c];
      if (ch > FIRST && ch < 0x7f)
        ctx.drawImage(atlas, (ch - FIRST) * CW * SCALE, 0, CW * SCALE, CH * SCALE, c * CW, r * CH, CW, CH);
    }
  }

  private drawCursor(): void {
    const { col, row } = this.cursorAt;
    if (col >= COLS) return;
    const ctx = this.ctx;
    ctx.fillStyle = this.cursorOn || !this.blinks ? PHOSPHORS[this.phosphor].fg : "#000";
    ctx.fillRect(col * CW, row * CH + 13, CW, 2);
  }

  private buildAtlas(): HTMLCanvasElement {
    const atlas = document.createElement("canvas");
    atlas.width = GLYPHS * CW * SCALE;
    atlas.height = CH * SCALE;
    const g = atlas.getContext("2d")!;
    g.scale(SCALE, SCALE);
    const fg = PHOSPHORS[this.phosphor].fg;
    g.font = "20px VT323, 'Courier New', monospace";
    g.textBaseline = "top";
    g.fillStyle = fg;
    g.shadowColor = fg;
    g.shadowBlur = 4;
    const sx = CW / (g.measureText("M").width || CW);
    for (let i = 0; i < GLYPHS; ++i) {
      g.save();
      g.translate(i * CW, -1);
      g.scale(sx, 1);
      g.fillText(String.fromCharCode(FIRST + i), 0, 0);
      g.restore();
    }
    return atlas;
  }

  private noSignal(): void {
    const ctx = this.ctx;
    const fg = PHOSPHORS[this.phosphor].fg;
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, W, H);
    ctx.fillStyle = fg;
    ctx.shadowColor = fg;
    ctx.shadowBlur = 6;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = "48px VT323, 'Courier New', monospace";
    ctx.fillText("NO SIGNAL", W / 2, H / 2 - 16);
    ctx.shadowBlur = 0;
    ctx.font = "20px VT323, 'Courier New', monospace";
    ctx.fillText("video card not emulated yet (hw/design.md, Emulator step 4)", W / 2, H / 2 + 28);
    ctx.textAlign = "start";
  }
}
