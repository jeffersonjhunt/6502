/**
 * The CRT: draws the terminal, or another scene, on the 640x480 canvas
 * inside the monitor's glass (the scanlines, vignette and glow are CSS
 * overlays, styles/crt.css).
 *
 * Text is drawn from a glyph atlas made once per phosphor colour (VT323,
 * whose 20 px glyphs are VGA's 8 pixels wide: a crisp core over a soft
 * halo), and only rows that changed are redrawn, so an idle screen costs
 * nothing per frame.
 *
 * Drawing is in VGA's 640x480, but the canvas holds a whole multiple of
 * that, at least as many pixels as the glass shows on this display
 * (resize()): the browser only ever scales the picture down, never blurs
 * it up, which on a 2x display it otherwise does by half again.
 *
 * Scenes are the monitor's input select. "text" is the serial terminal.
 * "vga" is the video card: its frames (setVideo()), drawn pixel for pixel
 * at the canvas's scale, in colour, or on a monochrome phosphor as a mono
 * monitor shows them (brightness in the phosphor's colour). Without a card
 * it shows NO SIGNAL. "apple" is Apple II mode as an Apple monitor shows
 * it: the video card's frame cut to the IIe's 560x384 picture and filling
 * the screen, as the Apple's 280x192 filled a TV; NO SIGNAL while the
 * machine is not in Apple II mode (setApple()).
 */

import type { VideoFrame } from "../port/machine-port";
import { COLS, ROWS, type Terminal } from "./terminal";

export type Scene = "text" | "vga" | "apple";
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
const MAX_SCALE = 4;
/** Where the video card puts Apple II mode's 560x384 picture (hw/design.md). */
export const APPLE_PICTURE = { x: 40, y: 48, w: 560, h: 384 };

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
  private apple = false;
  private readonly blinks: boolean;
  /** Canvas pixels per VGA pixel. */
  private k = 0;
  /** The video card's last frame as it came, and as this phosphor shows it. */
  private videoIn: Uint8ClampedArray | null = null;
  private videoDirty = false;
  private readonly videoCanvas: HTMLCanvasElement;
  private readonly videoImage: ImageData;

  constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly terminal: Terminal,
    options: { reducedMotion?: boolean } = {},
  ) {
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("no 2D canvas");
    this.ctx = ctx;
    this.blinks = !options.reducedMotion;
    this.videoCanvas = document.createElement("canvas");
    this.videoCanvas.width = W;
    this.videoCanvas.height = H;
    this.videoImage = new ImageData(W, H);
    this.resize(W, 1);
  }

  setScene(scene: Scene): void {
    this.scene = scene;
    this.full = true;
  }

  setPhosphor(phosphor: Phosphor): void {
    this.phosphor = phosphor;
    this.atlas = null;
    this.full = true;
    this.videoDirty = this.videoIn !== null;
  }

  /** A new frame from the video card (FrameState.video); kept until the next. */
  setVideo(frame: VideoFrame): void {
    this.videoIn = frame.pixels;
    this.videoDirty = true;
  }

  /** The video card's picture is gone (power off, or not watched): NO SIGNAL until a frame comes. */
  clearVideo(): void {
    this.videoIn = null;
    this.full = true;
  }

  /** The machine is in Apple II mode: the Apple II scene has a picture. */
  setApple(on: boolean): void {
    if (on !== this.apple) this.full = true;
    this.apple = on;
  }

  setPowered(on: boolean): void {
    if (on !== this.powered) this.full = true;
    this.powered = on;
  }

  /**
   * The canvas is shown `cssWidth` CSS pixels wide at `dpr` device pixels
   * each: hold enough pixels for that, a whole multiple of VGA's.
   */
  resize(cssWidth: number, dpr: number): void {
    const k = Math.min(MAX_SCALE, Math.max(1, Math.ceil((cssWidth * dpr) / W - 0.05)));
    if (k === this.k) return;
    this.k = k;
    this.canvas.width = W * k;
    this.canvas.height = H * k;
    this.canvas.dataset.scale = String(k);
    this.ctx.setTransform(k, 0, 0, k, 0, 0);
    this.atlas = null;
    this.full = true;
    // Resizing wiped the canvas: draw again now, not on the next animation
    // frame, which may be a while coming (a throttled or busy page).
    this.draw(performance.now());
  }

  /** Canvas pixels per VGA pixel (for tests). */
  get scale(): number {
    return this.k;
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
    if (this.scene === "vga") {
      if (this.videoIn && (this.videoDirty || this.full)) this.drawVideo();
      else if (!this.videoIn && this.full) this.noSignal("no video card on this machine");
      this.full = false;
      return;
    }
    if (this.scene === "apple") {
      if (this.apple && this.videoIn && (this.videoDirty || this.full)) this.drawVideo(APPLE_PICTURE);
      else if ((!this.apple || !this.videoIn) && this.full)
        this.noSignal(this.apple ? "no video card on this machine" : "not in Apple II mode: insert a IIe ROM, then Start Apple IIe");
      this.full = false;
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
        ctx.drawImage(atlas, (ch - FIRST) * CW * this.k, 0, CW * this.k, CH * this.k, c * CW, r * CH, CW, CH);
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
    atlas.width = GLYPHS * CW * this.k;
    atlas.height = CH * this.k;
    const g = atlas.getContext("2d")!;
    g.scale(this.k, this.k);
    const fg = PHOSPHORS[this.phosphor].fg;
    g.font = "20px VT323, 'Courier New', monospace";
    g.textBaseline = "top";
    g.fillStyle = fg;
    const sx = CW / (g.measureText("M").width || CW); // 1 for VT323; squeezes a fallback font
    const glyphs = () => {
      for (let i = 0; i < GLYPHS; ++i) {
        g.save();
        g.translate(i * CW, -1);
        g.scale(sx, 1);
        g.fillText(String.fromCharCode(FIRST + i), 0, 0);
        g.restore();
      }
    };
    // The phosphor's halo, soft and faint (shadowBlur is in canvas pixels)...
    g.shadowColor = fg;
    g.shadowBlur = 2.5 * this.k;
    g.globalAlpha = 0.55;
    glyphs();
    // ...then the beam itself, sharp.
    g.shadowBlur = 0;
    g.globalAlpha = 1;
    glyphs();
    return atlas;
  }

  /** The frame, through the phosphor: all of it scaled up by whole pixels, or a part of it filling the screen. */
  private drawVideo(part: { x: number; y: number; w: number; h: number } = { x: 0, y: 0, w: W, h: H }): void {
    const src = this.videoIn!;
    const out = this.videoImage.data;
    const rgb = PHOSPHORS[this.phosphor].rgb;
    if (!rgb) out.set(src);
    else {
      for (let i = 0; i < out.length; i += 4) {
        const l = (77 * src[i] + 150 * src[i + 1] + 29 * src[i + 2]) >> 8; // luma, 0-255
        out[i] = (rgb[0] * l) >> 8;
        out[i + 1] = (rgb[1] * l) >> 8;
        out[i + 2] = (rgb[2] * l) >> 8;
        out[i + 3] = 255;
      }
    }
    this.videoCanvas.getContext("2d")!.putImageData(this.videoImage, 0, 0);
    const whole = part.w === W && part.h === H;
    this.ctx.imageSmoothingEnabled = !whole; // not whole pixels: soften, as the tube would
    this.ctx.drawImage(this.videoCanvas, part.x, part.y, part.w, part.h, 0, 0, W, H);
    this.videoDirty = false;
  }

  private noSignal(why: string): void {
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
    ctx.fillText(why, W / 2, H / 2 + 28);
    ctx.textAlign = "start";
  }
}
