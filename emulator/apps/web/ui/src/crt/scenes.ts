/**
 * The mockup's still picture of the Apple II modes, shown only with
 * ?demo=scenes for design review (Apple II mode is not emulated yet):
 * hi-res mixed mode, 280x192 doubled and centred, with artifact-colour
 * fringes.
 */

type Ctx = CanvasRenderingContext2D;

function offscreen(w: number, h: number): [HTMLCanvasElement, Ctx] {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  return [c, c.getContext("2d")!];
}

function drawApple(ctx: Ctx): void {
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, 640, 480);
  const [c, g] = offscreen(280, 192);
  g.fillStyle = "#000";
  g.fillRect(0, 0, 280, 192);
  const W = "#FFF", V = "#D35CFF", GR = "#2FD52F", O = "#FF6A2F";
  [[12, 10], [40, 30], [77, 8], [101, 44], [140, 17], [166, 36], [203, 9], [240, 28], [262, 52], [25, 60], [190, 62]].forEach(
    ([x, y], i) => {
      g.fillStyle = i % 3 === 0 ? W : x % 2 ? V : GR;
      g.fillRect(x, y, i % 3 === 0 ? 2 : 1, 1);
    });
  const ridge = (base: number, amp: number, f: number, ph: number, colA: string, colB: string | null) => {
    for (let x = 0; x < 280; x++) {
      const top = base - Math.round(amp * Math.abs(Math.sin(x / f + ph)) + amp * 0.4 * Math.sin(x / (f / 3)));
      for (let y = top; y < 160; y++) {
        if (y === top || y === top + 1) {
          g.fillStyle = W;
          g.fillRect(x, y, 1, 1);
        } else if (colB === null || x % 2 === 0) {
          g.fillStyle = colB === null ? colA : y % 4 < 2 ? colA : colB;
          g.fillRect(x, y, 1, 1);
        }
      }
    }
  };
  ridge(128, 34, 26, 0.6, O, V);
  ridge(146, 14, 13, 2.1, GR, null);
  g.fillStyle = W;
  g.fillRect(186, 143, 34, 2);
  g.fillStyle = GR;
  g.fillRect(188, 145, 2, 4);
  g.fillRect(216, 145, 2, 4);
  ["....WWWW....", "...WWWWWW...", "..WW.WW.WW..", "..WWWWWWWW..", "...WWWWWW...", "..W.WWWW.W..", ".W...WW...W.",
   "W....OO....W", "W.....O....W"].forEach((row, y) => {
    for (let i = 0; i < row.length; i++)
      if (row[i] !== ".") {
        g.fillStyle = row[i] === "O" ? O : W;
        g.fillRect(160 + i, 74 + y, 1, 1);
      }
  });
  g.fillStyle = "#000";
  g.fillRect(0, 160, 280, 32);
  g.font = "8px VT323, monospace";
  g.textBaseline = "top";
  g.fillStyle = W;
  ["FUEL 0412   ALT 1880   VEL -012   T+047", "", "]BLOAD LANDER,A$2000", "]CALL 2048"].forEach((s, r) => {
    for (let i = 0; i < s.length; i++) g.fillText(s[i], i * 7, 160 + r * 8);
  });
  g.fillRect(70, 184, 7, 7);
  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(c, 40, 48, 560, 384);
}

function tint(ctx: Ctx, rgb: [number, number, number]): void {
  const im = ctx.getImageData(0, 0, ctx.canvas.width, ctx.canvas.height);
  const d = im.data;
  for (let i = 0; i < d.length; i += 4) {
    const l = Math.pow((0.3 * d[i] + 0.59 * d[i + 1] + 0.11 * d[i + 2]) / 255, 0.8);
    d[i] = rgb[0] * l;
    d[i + 1] = rgb[1] * l;
    d[i + 2] = rgb[2] * l;
  }
  ctx.putImageData(im, 0, 0);
}

/** A monochrome phosphor (`rgb`) tints the picture; null keeps colour. */
export function drawDemo(ctx: Ctx, rgb: [number, number, number] | null): void {
  drawApple(ctx);
  if (rgb) tint(ctx, rgb);
}
