/**
 * The mockup's still pictures of the video card's modes, shown only with
 * ?demo=scenes for design review (the video card is not emulated yet).
 * Native tiles: 320x240 with a 12-bit palette, doubled. Apple II hi-res
 * mixed mode: 280x192 doubled and centred, with artifact-colour fringes.
 */

type Ctx = CanvasRenderingContext2D;

function offscreen(w: number, h: number): [HTMLCanvasElement, Ctx] {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  return [c, c.getContext("2d")!];
}

function drawTiles(ctx: Ctx): void {
  const [c, g] = offscreen(320, 240);
  ["#124", "#135", "#236", "#347", "#358", "#469", "#57A", "#68B", "#8AC"].forEach((s, i) => {
    g.fillStyle = s;
    g.fillRect(0, i * 18, 320, 18);
  });
  g.fillStyle = "#EEF";
  for (const [x, y] of [[20, 8], [61, 22], [104, 6], [150, 30], [210, 12], [260, 26], [301, 9], [233, 44], [40, 47]])
    g.fillRect(x, y, 1, 1);
  g.fillStyle = "#FEC";
  g.beginPath();
  g.arc(262, 40, 14, 0, 7);
  g.fill();
  g.fillStyle = "#358";
  g.beginPath();
  g.arc(256, 36, 12, 0, 7);
  g.fill();
  const hills = (color: string, start: number, f: (x: number) => number) => {
    g.fillStyle = color;
    g.beginPath();
    g.moveTo(0, start);
    for (let x = 0; x <= 320; x += 8) g.lineTo(x, f(x));
    g.lineTo(320, 240);
    g.lineTo(0, 240);
    g.fill();
  };
  hills("#235", 150, (x) => 132 + Math.round(10 * Math.sin(x / 23) + 6 * Math.sin(x / 9)));
  hills("#143", 170, (x) => 160 + Math.round(8 * Math.sin(x / 31 + 2)));
  const brick = (tx: number, ty: number, a: string, b: string, d: string) => {
    g.fillStyle = a;
    g.fillRect(tx, ty, 16, 16);
    g.fillStyle = b;
    g.fillRect(tx, ty, 16, 2);
    g.fillRect(tx, ty, 2, 16);
    g.fillStyle = d;
    g.fillRect(tx, ty + 7, 16, 1);
    g.fillRect(tx + 8, ty, 1, 7);
    g.fillRect(tx + 3, ty + 8, 1, 8);
    g.fillRect(tx, ty + 15, 16, 1);
  };
  for (let x = 0; x < 320; x += 16) {
    g.fillStyle = "#3A5";
    g.fillRect(x, 192, 16, 4);
    g.fillStyle = "#5C6";
    g.fillRect(x, 192, 16, 1);
    brick(x, 196, "#A52", "#C74", "#622");
    brick(x, 212, "#A52", "#C74", "#622");
    brick(x, 228, "#933", "#B54", "#511");
  }
  for (const [x, y, n] of [[64, 136, 4], [176, 112, 3], [240, 152, 2]])
    for (let i = 0; i < n; i++) brick(x + i * 16, y, "#C83", "#EB5", "#742");
  for (const [x, y] of [[96, 88], [192, 64]]) {
    g.fillStyle = "#C50";
    g.fillRect(x, y, 16, 16);
    g.fillStyle = "#F82";
    g.fillRect(x, y, 16, 1);
    g.fillRect(x, y, 1, 16);
    g.fillStyle = "#310";
    g.fillRect(x + 15, y, 1, 16);
    g.fillRect(x, y + 15, 16, 1);
    g.fillStyle = "#FFF";
    for (const ox of [3, 9]) {
      g.fillRect(x + ox, y + 5, 4, 1);
      g.fillRect(x + ox, y + 10, 4, 1);
      g.fillRect(x + ox, y + 5, 1, 6);
      g.fillRect(x + ox + 3, y + 5, 1, 6);
    }
  }
  for (const [x, y] of [[72, 118], [88, 118], [104, 118], [188, 94], [204, 94], [248, 136]]) {
    g.fillStyle = "#4EF";
    g.fillRect(x + 2, y, 4, 1);
    g.fillRect(x + 1, y + 1, 6, 2);
    g.fillRect(x + 2, y + 3, 4, 2);
    g.fillRect(x + 3, y + 5, 2, 1);
    g.fillStyle = "#FFF";
    g.fillRect(x + 2, y + 1, 1, 1);
  }
  const sprite = (map: string[], pal: Record<string, string>, ox: number, oy: number, flip = false) =>
    map.forEach((row, y) => {
      for (let i = 0; i < 16; i++) {
        const ch = row[flip ? 15 - i : i];
        if (ch !== ".") {
          g.fillStyle = pal[ch];
          g.fillRect(ox + i, oy + y, 1, 1);
        }
      }
    });
  sprite(
    ["......8888......", ".....8FFFF8.....", "....8FFFFFF8....", "....8F0FF0F8....", "....8FFFFFF8....", ".....8FFFF8.....",
     "...88CCCCCC88...", "..8CCCCCCCCCC8..", "..8CC8CCCC8CC8..", "..8CC8CCCC8CC8..", "...8CCCCCCCC8...", "....8CCCCCC8....",
     "....8DD88DD8....", "....8DD8.8DD8...", "...88DD8.8DD88..", "...8888...8888.."],
    { "8": "#111", F: "#EEE", "0": "#16C", C: "#C50", D: "#555" }, 120, 176);
  sprite(
    ["................", "................", "................", "................", "................", "................",
     "......8888......", "....88AAAA88....", "...8AAAAAAAA8...", "..8AAFFAAFFAA8..", "..8AAF0AAF0AA8..", "..8AAAAAAAAAA8..",
     "..8AAA8888AAA8..", "...8AAAAAAAA8...", "....88888888....", "................"],
    { "8": "#111", A: "#A3C", F: "#FFF", "0": "#111" }, 216, 180, true);
  g.fillStyle = "rgba(0,0,0,.55)";
  g.fillRect(0, 0, 320, 14);
  g.font = "8px VT323, monospace";
  g.textBaseline = "top";
  g.fillStyle = "#FFF";
  g.fillText("SCORE 004200", 6, 3);
  g.fillStyle = "#4EF";
  g.fillText("x07", 132, 3);
  g.fillStyle = "#F82";
  g.fillText("ZONE 1-1", 250, 3);
  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(c, 0, 0, 640, 480);
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
export function drawDemo(ctx: Ctx, scene: "tiles" | "apple", rgb: [number, number, number] | null): void {
  if (scene === "tiles") drawTiles(ctx);
  else drawApple(ctx);
  if (rgb) tint(ctx, rgb);
}
