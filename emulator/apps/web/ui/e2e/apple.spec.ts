import { expect, test, type Page } from "@playwright/test";
import { openDebugger, openSettings } from "./ui";
import { fakeIIeRom } from "../src/port/fake-iie";

// Apple II mode in the browser, with a stand-in IIe ROM (Apple's is never
// in the repo): the cartridge, Start Apple IIe, the scene, the lamp, the
// Devices tab, and the ROM kept in this browser across a reload.

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
}

const lamp = (page: Page, name: string) =>
  page.locator(`.lamp[data-lamp="${name}"]`).evaluate((e) => Number((e as HTMLElement).style.getPropertyValue("--b") || 0));

/** Lit canvas pixels in a box given in the screen's 640x480. */
function lit(page: Page, x: number, y: number, w: number, h: number) {
  return page.evaluate(
    ([x, y, w, h]) => {
      const c = document.querySelector<HTMLCanvasElement>("[data-testid=crt] canvas")!;
      const k = c.width / 640;
      const d = c.getContext("2d")!.getImageData(x * k, y * k, w * k, h * k).data;
      let n = 0;
      for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 150) n++;
      return n;
    },
    [x, y, w, h],
  );
}

async function insert(page: Page) {
  await page.getByTestId("apple-file").setInputFiles({ name: "iie.rom", mimeType: "application/octet-stream", buffer: Buffer.from(fakeIIeRom()) });
  await expect(page.getByTestId("apple-meta")).toHaveText("16 KB · yours, kept in this browser");
}

test("a IIe ROM in, Start Apple IIe: the Apple's picture, AMODE, its clock and switches", async ({ page }) => {
  await booted(page);
  await openSettings(page);
  await expect(page.getByTestId("apple-start")).toBeHidden(); // nothing to start without a ROM
  await insert(page);
  await expect(page.getByTestId("apple-note")).toContainText("No character ROM");
  await page.getByTestId("apple-start").click();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "apple");
  await expect(page.getByTestId("mon-label")).toHaveText("Display · Apple IIe 560×384");
  await expect(page.getByRole("button", { name: "Display" })).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByTestId("crt")).toHaveAttribute("aria-label", /Apple IIe screen/);
  await expect(page.getByTestId("apple-reset")).toBeVisible();
  await expect(page.getByTestId("apple-start")).toBeHidden();
  await expect.poll(() => lamp(page, "AMODE")).toBe(1);
  const clock = page.getByRole("group", { name: "Clock" });
  await expect(clock.getByRole("button", { name: "1.02" })).toHaveAttribute("aria-pressed", "true");
  // The stand-in's "A" at $0400: the first 14 of 560 dots and 16 of 384 lines, filling the screen: 16 x 20.
  await expect.poll(() => lit(page, 0, 0, 16, 20)).toBeGreaterThan(10);
  await page.screenshot({ path: `test-results/apple-${test.info().project.name}.png` });
  expect(await lit(page, 20, 0, 620, 480)).toBe(0); // and spaces everywhere else
  expect(await lit(page, 0, 24, 20, 456)).toBe(0);

  await page.getByTestId("apple-reset").click(); // Ctrl-Reset, in the strip: still the Apple
  await page.waitForTimeout(200);
  expect(await lamp(page, "AMODE")).toBe(1);

  await openDebugger(page);
  await page.locator('[data-tab="dev"]').click();
  const dev = page.getByTestId("dev-apple");
  await expect(dev.locator('[data-reg="Mode"]')).toHaveText("on · the IIe's map · 1.02 MHz");
  await expect(dev.locator('[data-reg="Video"]')).toHaveText("text 40 · page 1 · MouseText");
  // The cards, still read in I/O space; the /RESET that entered the mode cleared the video card's CTRL (the Apple's
  // picture doesn't use it).
  await expect(page.getByTestId("dev-video")).toContainText("CTRL$00  none");
  // START: out of Apple II mode, back on the BIOS's screen, and the monitor follows it to Terminal.
  await page.locator('[data-ctl="start"]').click();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "text", { timeout: 10_000 });
  await expect(page.getByTestId("apple-reset")).toBeHidden();
});

test("the ROM is kept in this browser: back after a reload; Eject forgets it", async ({ page }) => {
  await booted(page);
  await insert(page);
  await page.reload();
  await expect(page.getByTestId("apple-meta")).toHaveText("16 KB · yours, kept in this browser", { timeout: 15_000 });
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await page.getByTestId("crt").click();
  await page.keyboard.type("C806R"); // typed at the monitor works as well as the button
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "apple");
  await openSettings(page);
  page.once("dialog", (d) => void d.accept()); // in Apple II mode it asks: the IIe's program would go
  await page.getByTestId("apple-eject").click();
  await expect(page.getByTestId("apple-meta")).toHaveText(/^none/);
  await expect(page.getByTestId("apple-insert")).toBeVisible();
  expect(await page.evaluate(() => Object.keys(localStorage).filter((k) => k.startsWith("oo1.apple")))).toEqual([]);
  await page.reload();
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await expect(page.getByTestId("apple-meta")).toHaveText(/^none/);
});

test("a file that is no IIe ROM: says why", async ({ page }) => {
  await booted(page);
  await page.getByTestId("apple-file").setInputFiles({ name: "notes.txt", mimeType: "text/plain", buffer: Buffer.from("hello") });
  await expect(page.getByTestId("apple-note")).toHaveText(/^notes\.txt: 5 bytes is no IIe ROM/);
  await expect(page.getByTestId("apple-meta")).toHaveText(/^none/);
});
