import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { expect, test, type Page } from "@playwright/test";
import { openDebugger, openSettings, phosphor } from "./ui";

// The real enhanced IIe in the browser, when the owner's ROMs are in the
// gitignored fixtures (test/host/fixtures/apple2e, index.txt): picked as
// files, as a visitor would, and never part of the build. Skipped elsewhere.
// OO1_IIE_DIR names another folder of them (a worktree has no fixtures).

const dir = process.env.OO1_IIE_DIR ? `${process.env.OO1_IIE_DIR}/` : fileURLToPath(new URL("../../../../test/host/fixtures/apple2e/", import.meta.url));
const ROM = "iie-enhanced-c000-ffff.bin";
const CHARS = "iie-video-enhanced-342-0265-a.bin";
const P5 = "disk2-16-p5-boot-341-0027-a.bin";
const P6 = "disk2-16-p6-sequencer-341-0028-a.bin";
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
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await page.getByTestId("apple-file").setInputFiles([
    { name: ROM, mimeType: "application/octet-stream", buffer: readFileSync(dir + ROM) },
    { name: CHARS, mimeType: "application/octet-stream", buffer: readFileSync(dir + CHARS) },
  ]);
  await expect(page.getByTestId("apple-meta")).toHaveText("16 KB · yours, kept in this browser");
  await expect(page.getByTestId("apple-note")).toHaveText("No Disk II PROMs: the drive needs its boot ROM and sequencer");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await openSettings(page);
  await page.getByTestId("apple-start").click();
  await expect.poll(() => row(page, 0), { timeout: 10_000 }).toBeGreaterThan(50); // "Apple //e"
  await phosphor(page, "Colour");
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

/** A screen pixel (x, y in 640x480), R G B. */
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

test("a program pasted into the IIe, faster than Applesoft takes it, arrives whole and runs", async ({ page }) => {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await page.getByTestId("apple-file").setInputFiles([
    { name: ROM, mimeType: "application/octet-stream", buffer: readFileSync(dir + ROM) },
    { name: CHARS, mimeType: "application/octet-stream", buffer: readFileSync(dir + CHARS) },
  ]);
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await openSettings(page);
  await page.getByTestId("apple-start").click();
  await expect.poll(() => row(page, 0), { timeout: 10_000 }).toBeGreaterThan(50);
  await phosphor(page, "Colour");
  await page.getByTestId("crt").click();
  const program = "10 GR\n20 FOR I = 0 TO 15: COLOR= I\n30 FOR X = I * 2 + 4 TO I * 2 + 5: VLIN 0,39 AT X: NEXT: NEXT\nRUN\n";
  await page.getByTestId("crt").evaluate((glass, text) => {
    const data = new DataTransfer();
    data.setData("text/plain", text);
    glass.dispatchEvent(new ClipboardEvent("paste", { clipboardData: data, bubbles: true, cancelable: true }));
  }, program);
  // Lo-res block x is 14 of 560 dots: 16 screen pixels. Bar I is blocks 2I+4 and 2I+5.
  const bar = (i: number) => pixel(page, (2 * i + 5) * 16, 200);
  await expect.poll(() => bar(15), { timeout: 20_000 }).toEqual([255, 255, 255]); // the last bar: white
  expect(await bar(0)).toEqual([0, 0, 0]);
  expect(await bar(1)).toEqual([238, 34, 102]); // magenta, $E26
  expect(await bar(12)).toEqual([17, 255, 68]); // green, $1F4
  await page.screenshot({ path: `test-results/iie-lores-${test.info().project.name}.png` });
});

test("the Disk II boots The Oregon Trail from the owner's WOZ, the drive light on while it loads", async ({ page }) => {
  const woz = `${process.env.OO1_DISKS_DIR ?? fileURLToPath(new URL("../../../../test/host/fixtures/disks/", import.meta.url))}/The Oregon Trail - Disk 1, Side A.woz`;
  test.skip(!existsSync(woz) || !existsSync(dir + P5) || !existsSync(dir + P6), "no WOZ or Disk II PROMs among the fixtures");
  test.setTimeout(120_000);
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await page.getByTestId("apple-file").setInputFiles([
    { name: ROM, mimeType: "application/octet-stream", buffer: readFileSync(dir + ROM) },
    { name: CHARS, mimeType: "application/octet-stream", buffer: readFileSync(dir + CHARS) },
    { name: P5, mimeType: "application/octet-stream", buffer: readFileSync(dir + P5) },
    { name: P6, mimeType: "application/octet-stream", buffer: readFileSync(dir + P6) },
  ]);
  await expect(page.getByTestId("apple-note")).toHaveText("");          // nothing missing
  await page.getByTestId("disk-file").setInputFiles({ name: "The Oregon Trail.woz", mimeType: "application/octet-stream", buffer: readFileSync(woz) });
  await expect(page.getByTestId("disk-name")).toHaveText("Drive 1 · The Oregon Trail.woz");
  await expect(page.getByTestId("disk-note")).toHaveText("Start Apple IIe boots it");
  await phosphor(page, "Colour");
  await openSettings(page);
  await page.getByTestId("apple-start").click();
  await expect(page.getByTestId("disk-light")).toHaveClass(/on/, { timeout: 10_000 });
  await expect(page.getByTestId("disk-note")).toHaveText("");
  await openDebugger(page);
  await page.locator('[data-tab="dev"]').click();
  const video = page.getByTestId("dev-apple").locator('[data-reg="Video"]');
  await expect(video).toHaveText("hi-res · page 2", { timeout: 30_000 });
  await expect(page.getByTestId("disk-light")).not.toHaveClass(/on/, { timeout: 60_000 }); // loaded: the drive stops
  // "The Oregon Trail" in white across the top of the menu.
  await expect.poll(() => lit(page, 0, 20, 640, 50)).toBeGreaterThan(2000);
  await page.screenshot({ path: `test-results/oregon-trail-${test.info().project.name}.png` });
});

test("a disk put in at BASIC's prompt boots on Reboot, and the drive says so", async ({ page }) => {
  const woz = `${process.env.OO1_DISKS_DIR ?? fileURLToPath(new URL("../../../../test/host/fixtures/disks/", import.meta.url))}/The Oregon Trail - Disk 1, Side A.woz`;
  test.skip(!existsSync(woz) || !existsSync(dir + P5) || !existsSync(dir + P6), "no WOZ or Disk II PROMs among the fixtures");
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await page.getByTestId("apple-file").setInputFiles([
    { name: ROM, mimeType: "application/octet-stream", buffer: readFileSync(dir + ROM) },
    { name: P5, mimeType: "application/octet-stream", buffer: readFileSync(dir + P5) },
    { name: P6, mimeType: "application/octet-stream", buffer: readFileSync(dir + P6) },
  ]);
  await openSettings(page);
  await page.getByTestId("apple-start").click();
  await expect(page.getByTestId("disk-note")).toHaveText(/^No disk: the IIe waits for one/, { timeout: 10_000 });
  await page.getByTestId("apple-reset").click(); // to BASIC
  await expect(page.getByTestId("disk-light")).not.toHaveClass(/on/, { timeout: 10_000 });
  await page.getByTestId("disk-file").setInputFiles({ name: "The Oregon Trail.woz", mimeType: "application/octet-stream", buffer: readFileSync(woz) });
  await expect(page.getByTestId("disk-note")).toHaveText("Reboot boots it (or PR#6 in BASIC)");
  await page.waitForTimeout(1000);
  await expect(page.getByTestId("disk-light")).not.toHaveClass(/on/); // in, and nothing reads it: a real IIe's way
  await page.getByTestId("apple-reboot").click();
  await expect(page.getByTestId("disk-light")).toHaveClass(/on/, { timeout: 10_000 });
  await expect(page.getByTestId("disk-note")).toHaveText("");
});
