import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { expect, test, type Page } from "@playwright/test";

// The real enhanced IIe in the browser, when the owner's ROMs are in the
// gitignored fixtures (test/host/fixtures/apple2e, index.txt): picked as
// files, as a visitor would, and never part of the build. Skipped elsewhere.
// OO1_IIE_DIR names another folder of them (a worktree has no fixtures).

const dir = process.env.OO1_IIE_DIR ? `${process.env.OO1_IIE_DIR}/` : fileURLToPath(new URL("../../../../test/host/fixtures/apple2e/", import.meta.url));
const ROM = "iie-enhanced-c000-ffff.bin";
const CHARS = "iie-video-enhanced-342-0265-a.bin";
const have = existsSync(dir + ROM) && existsSync(dir + CHARS);

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

/** One of the IIe's 24 text rows, 20 screen lines each once it fills 480. */
const row = (page: Page, r: number) => lit(page, 0, r * 20, 640, 20);

test.skip(!have, "the owner's IIe ROMs are not in test/host/fixtures/apple2e");

test("the owner's enhanced IIe: boots to Applesoft, answers, Ctrl-Reset keeps the screen", async ({ page }) => {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.6", { timeout: 15_000 });
  await page.getByTestId("apple-file").setInputFiles([
    { name: ROM, mimeType: "application/octet-stream", buffer: readFileSync(dir + ROM) },
    { name: CHARS, mimeType: "application/octet-stream", buffer: readFileSync(dir + CHARS) },
  ]);
  await expect(page.getByTestId("apple-insert")).toHaveText("IIe ROM ✓");
  await expect(page.getByTestId("apple-note")).toHaveText("");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.6", { timeout: 15_000 });
  await page.getByTestId("apple-start").click();
  await expect.poll(() => row(page, 0), { timeout: 10_000 }).toBeGreaterThan(50); // "Apple //e"
  await page.getByRole("button", { name: "Colour" }).click();
  await page.waitForTimeout(1500);
  let prompt = -1; // the last lit row: the "]" and its cursor
  for (let r = 0; r < 24; ++r) if ((await row(page, r)) > 0) prompt = r;
  expect(prompt).toBeGreaterThan(0);
  await page.getByTestId("crt").click(); // the keyboard back on the screen, from the Colour button
  await page.keyboard.type("PRINT 2");
  await page.keyboard.press("Shift+Equal"); // "+" as a person types it (type() leaves Shift up)
  await page.keyboard.type("2");
  await page.keyboard.press("Enter");
  await page.waitForTimeout(1000);
  await page.screenshot({ path: `test-results/iie-${test.info().project.name}.png` });
  await expect.poll(() => row(page, prompt + 1)).toBeGreaterThan(20); // "4"
  // Ctrl-Reset in Applesoft: a fresh "]" on the bottom line (scrolling the
  // banner off), and the screen as it was above it: no reboot.
  await page.getByTestId("apple-reset").click();
  await expect.poll(() => row(page, 23)).toBeGreaterThan(0);
  await expect(page.getByTestId("crt")).toBeFocused(); // typing goes on at once
  expect(await row(page, prompt)).toBeGreaterThan(20); // "4", one line up after the scroll
});
