import { expect, test, type Page } from "@playwright/test";

// The VGA scene: the video card's frames, as the BIOS draws its screen.

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.6", { timeout: 15_000 });
}

/** A VGA pixel (x, y in 640x480) off the canvas, which holds k x k canvas pixels for each. */
function pixel(page: Page, x: number, y: number) {
  return page.evaluate(
    ([x, y]) => {
      const c = document.querySelector<HTMLCanvasElement>("[data-testid=crt] canvas")!;
      const k = c.width / 640;
      const d = c.getContext("2d")!.getImageData(x * k + Math.floor(k / 2), y * k + Math.floor(k / 2), 1, 1).data;
      return [d[0], d[1], d[2]];
    },
    [x, y],
  );
}

/** Lit VGA pixels in a text row (16 lines of 640), sampled every other pixel. */
function litInRow(page: Page, row: number) {
  return page.evaluate((row) => {
    const c = document.querySelector<HTMLCanvasElement>("[data-testid=crt] canvas")!;
    const k = c.width / 640;
    const d = c.getContext("2d")!.getImageData(0, row * 16 * k, 640 * k, 16 * k).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4 * k) if (d[i] + d[i + 1] + d[i + 2] > 150) n++;
    return n;
  }, row);
}

test("VGA shows the BIOS's screen pixel for pixel, in the phosphor's colour", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "VGA" }).click();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "vga");
  // The B of BACKPLANE (row 1): glyph row 2 is 11111100. P3 amber, at light grey's brightness.
  await expect.poll(() => pixel(page, 0, 18)).toEqual([169, 116, 0]);
  expect(await pixel(page, 6, 18)).toEqual([0, 0, 0]);
  await page.getByRole("button", { name: "Colour" }).click();
  await expect.poll(() => pixel(page, 0, 18)).toEqual([170, 170, 170]); // the CGA's light grey, as the card made it
  await page.screenshot({ path: `test-results/vga-${test.info().project.name}.png` });
});

test("typing shows on VGA too, and the scene is remembered", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "VGA" }).click();
  await expect.poll(() => pixel(page, 0, 18)).not.toEqual([0, 0, 0]);
  expect(await litInRow(page, 4)).toBe(0);
  await page.getByTestId("crt").click();
  await page.keyboard.type("C680.C684");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText("C680: 00 01 02 7F 7F");
  await expect.poll(() => litInRow(page, 3)).toBeGreaterThan(40); // the command, under the prompt
  await expect.poll(() => litInRow(page, 5)).toBeGreaterThan(40); // the answer
  await page.reload();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "vga");
  await expect.poll(() => pixel(page, 0, 18), { timeout: 15_000 }).not.toEqual([0, 0, 0]);
});

test("the Devices tab decodes the video card's registers", async ({ page }) => {
  await booted(page);
  await page.locator('[data-tab="dev"]').click();
  const dev = page.getByTestId("dev-video");
  await expect(dev.locator('[data-reg="CTRL"]')).toHaveText("$03  display · layer 0");
  await expect(dev.locator('[data-reg="Layer 0"]')).toHaveText("text 80×30 · text $82000 · font $80400 · blink");
  await expect(dev.locator('[data-reg="Beam"]')).toContainText("line ");
  // The sound card, as the BIOS leaves it: nothing touches it yet.
  const sound = page.getByTestId("dev-sound");
  await expect(sound.locator('[data-reg="CTRL"]')).toHaveText("$00  PAL 0.985 MHz · 6510 stopped · SID B at $D420");
  await expect(sound.locator('[data-reg="SID A"]')).toHaveText("volume 0 · filter off");
  await expect(sound.locator('[data-reg="6510"]')).toContainText("· held"); // never started
  await expect(sound.locator('[data-reg="Timer A"]')).toHaveText("$FFFF of $FFFF · stopped");
});

test("the page credits the screen font", async ({ page }) => {
  await booted(page);
  const credits = page.getByTestId("credits");
  await expect(credits).toContainText("IBM VGA 8×16");
  await expect(credits.getByRole("link", { name: "VileR's Ultimate Oldschool PC Font Pack" })).toHaveAttribute(
    "href",
    "https://int10h.org/oldschool-pc-fonts/",
  );
  await expect(credits.getByRole("link", { name: "CC BY-SA 4.0" })).toBeVisible();
});

/** Distinct colours across the canvas, sampled on a coarse grid. */
function colours(page: Page) {
  return page.evaluate(() => {
    const c = document.querySelector<HTMLCanvasElement>("[data-testid=crt] canvas")!;
    const d = c.getContext("2d")!.getImageData(0, 0, c.width, c.height).data;
    const seen = new Set<number>();
    for (let y = 0; y < c.height; y += 9) for (let x = 0; x < c.width; x += 7) {
      const i = (y * c.width + x) * 4;
      seen.add((d[i] << 16) | (d[i + 1] << 8) | d[i + 2]);
    }
    return seen.size;
  });
}

test("C803R runs the video demo on VGA, and a key goes back to the monitor", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "VGA" }).click();
  await page.getByRole("button", { name: "Colour" }).click();
  await expect.poll(() => pixel(page, 0, 18)).toEqual([170, 170, 170]);
  expect(await colours(page)).toBeLessThan(5); // the text screen: grey on black
  await page.getByTestId("crt").click();
  await page.keyboard.type("C803R");
  await page.keyboard.press("Enter");
  await expect.poll(() => colours(page), { timeout: 10_000 }).toBeGreaterThan(12); // sky, hills, balls, caption
  await page.locator('[data-tab="dev"]').click();
  await expect(page.getByTestId("dev-video").locator('[data-reg="Sprites"]')).toContainText("on · table $89000");
  await page.screenshot({ path: `test-results/vga-demo-${test.info().project.name}.png` });
  await page.getByTestId("crt").click();
  await page.keyboard.press("x");
  await expect.poll(() => pixel(page, 0, 18), { timeout: 10_000 }).toEqual([170, 170, 170]); // the screen as it was
  await expect.poll(() => colours(page)).toBeLessThan(5);
  await expect(page.getByTestId("crt-text")).toContainText("C803: 4C\\");
});

test("on VGA the keyboard is the USB card's; on Text it is the serial line's", async ({ page }) => {
  await booted(page);
  await page.locator('[data-tab="dev"]').click();
  const key = page.getByTestId("dev-usb").locator('[data-reg="KEY"]');
  await expect(page.getByRole("button", { name: "CAPS" })).toBeEnabled();
  await page.getByTestId("crt").click();
  await page.keyboard.type("C680");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText("C680: 00");
  await expect(key).toHaveText("$00 ^@");                      // serial: the USB card saw nothing
  await page.getByRole("button", { name: "VGA" }).click();
  await expect(page.getByRole("button", { name: "CAPS" })).toBeDisabled();
  await page.getByTestId("crt").click();
  await page.keyboard.type("c684");                            // lowercase: the card's Caps Lock makes capitals
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText("C684: 7F");
  await expect(key).toHaveText("$0D CR");                      // the last key, taken by the BIOS
});

test("on VGA the mouse is the USB card's: the monitor reads its position", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "VGA" }).click();
  await page.getByTestId("crt").click();
  await page.keyboard.type("C590: 1");                         // MCTRL: absolute
  await page.keyboard.press("Enter");
  const box = (await page.locator("[data-testid=crt] canvas").boundingBox())!;
  // Inside VGA pixel (320, 240), clear of its edges: WebKit rounds the pointer to whole CSS pixels.
  await page.mouse.move(box.x + (320.9 / 640) * box.width, box.y + (240.9 / 480) * box.height);
  await page.keyboard.type("C592.C595");                       // reading MX latches MX and MY
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText("C592: 40 01 F0 00"); // 320, 240
});

test("Cmd shortcuts are the browser's, and a key held when focus goes is let go", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "VGA" }).click();
  await page.locator('[data-tab="dev"]').click();
  await page.getByTestId("crt").click();
  await page.keyboard.down("Shift");
  await expect(page.getByTestId("dev-usb").locator('[data-reg="Modifiers"]')).toHaveText("L-Shift");
  await page.getByRole("button", { name: "Text" }).click();    // focus leaves the screen
  await expect(page.getByTestId("dev-usb").locator('[data-reg="Modifiers"]')).toHaveText("none");
  await page.keyboard.up("Shift");
});
