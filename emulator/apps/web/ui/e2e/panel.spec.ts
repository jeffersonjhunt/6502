import { expect, test, type Page } from "@playwright/test";

async function booted(page: Page) {
  await page.goto("./");
  await page.evaluate(() => localStorage.clear());
  await page.reload();
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.5", { timeout: 15_000 });
}

const lamp = (page: Page, name: string) =>
  page.locator(`.lamp[data-lamp="${name}"]`).evaluate((e) => Number((e as HTMLElement).style.getPropertyValue("--b") || 0));
const sw = (page: Page, bit: number) => page.locator(`.sr[data-bit="${bit}"]`);
const ctl = (page: Page, id: string) => page.locator(`[data-ctl="${id}"]`);

/** Set the switch register to `value` by clicking the switches that differ. */
async function setSwitches(page: Page, value: number) {
  for (let bit = 0; bit < 20; bit++) {
    const up = (await sw(page, bit).getAttribute("aria-pressed")) === "true";
    if (up !== (((value >> bit) & 1) === 1)) await sw(page, bit).click();
  }
  await expect(page.getByTestId("readout-swr")).toHaveText(value.toString(16).toUpperCase().padStart(5, "0"));
}

test("the lamps glow with the monitor's loop: code at $FCBxx, the 6551 at $0C3xx, the USB card at $0C5xx", async ({ page }) => {
  await booted(page);
  await expect.poll(() => lamp(page, "A19")).toBeGreaterThan(0.3);
  expect(await lamp(page, "A14")).toBeGreaterThan(0.9); // high in all three (low only for the odd stack access)
  expect(await lamp(page, "A13")).toBeLessThan(0.3); // low in all three
  expect(await lamp(page, "POWER")).toBe(1);
  expect(await lamp(page, "RUN")).toBe(1);
  expect(await lamp(page, "HALT")).toBe(0);
  // The CRT is drawn too, not just the lamps: a resize wipes the canvas, and
  // under load WebKit was slow enough with the next frame to catch it blank.
  const lit = await page.evaluate(() => {
    const c = document.querySelector<HTMLCanvasElement>("[data-testid=crt] canvas")!;
    const d = c.getContext("2d")!.getImageData(0, 0, c.width, c.height / 4).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] > 60) n++;
    return n;
  });
  expect(lit).toBeGreaterThan(200);
  await page.screenshot({ path: `test-results/panel-${test.info().project.name}.png` });
});

test("HALT on the panel stops the CPU, freezes the lamps, and CONT steps", async ({ page }) => {
  await booted(page);
  await ctl(page, "halt").click();
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await expect.poll(() => lamp(page, "HALT")).toBe(1);
  expect(await lamp(page, "RUN")).toBe(0);
  await expect(page.getByTestId("readout-addr")).toHaveText(/^[0-9A-F]{5}$/);
  const seen = new Set<string>();
  for (let i = 0; i < 6; i++) {
    await ctl(page, "cont").click();
    await page.waitForTimeout(60);
    seen.add((await page.getByTestId("readout-addr").textContent())!);
  }
  expect(seen.size).toBeGreaterThan(1); // the latched address moves as it steps
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await ctl(page, "halt").click(); // ENABLE
  await ctl(page, "cont").click();
  await expect(page.getByTestId("status")).toContainText("RUNNING");
});

test("LOAD ADRS, DEP and EXAM through physical memory, and the monitor sees the deposit", async ({ page }) => {
  await booted(page);
  await ctl(page, "halt").click();
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await setSwitches(page, 0x00300);
  await ctl(page, "load").click();
  await setSwitches(page, 0x00342); // DEP takes bits 7-0: $42
  await ctl(page, "dep").click();
  await expect(page.getByTestId("readout-addr")).toHaveText("00300");
  await expect(page.getByTestId("readout-data")).toHaveText("42");
  await ctl(page, "dep").click(); // a second DEP steps on
  await expect(page.getByTestId("readout-addr")).toHaveText("00301");
  await setSwitches(page, 0x00300);
  await ctl(page, "load").click();
  await ctl(page, "exam").click();
  await expect(page.getByTestId("readout-data")).toHaveText("42");
  await page.getByTestId("tb-run").click();
  await page.getByTestId("crt").click();
  await page.keyboard.type("300.301");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText("0300: 42 42");
});

test("keyswitch: LOCK ignores the panel, OFF powers down, RUN is a cold start", async ({ page }) => {
  await booted(page);
  const key = page.getByTestId("keyswitch");
  await key.click(); // LOCK
  await ctl(page, "halt").click();
  await page.waitForTimeout(200);
  await expect(page.getByTestId("status")).toContainText("RUNNING"); // ignored
  await expect(page.getByTestId("tb-halt")).toBeDisabled();
  await key.click(); // OFF
  await expect(page.getByTestId("status")).toHaveText("POWER OFF");
  await expect.poll(() => lamp(page, "POWER")).toBe(0);
  expect(await lamp(page, "A19")).toBe(0);
  await key.click(); // RUN
  await expect(page.getByTestId("status")).toContainText("RUNNING");
  await expect(page.getByTestId("crt-text")).toHaveText(/^\s*BACKPLANE BIOS 0\.5\n\\$/); // a fresh screen
  await expect(page.getByTestId("tb-halt")).toBeEnabled();
});

test("the switch register stays where it was left", async ({ page }) => {
  await booted(page);
  await setSwitches(page, 0x0c680);
  await page.reload();
  await expect(page.getByTestId("readout-swr")).toHaveText("0C680");
  await expect(sw(page, 7)).toHaveAttribute("aria-pressed", "true");
});
