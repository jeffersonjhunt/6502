import { expect, test, type Page } from "@playwright/test";

const GREETING = "BACKPLANE BIOS 0.7";

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
}

/** A monitor line, and its echo. */
async function monitor(page: Page, line: string) {
  await page.getByTestId("crt").click();
  await page.keyboard.type(line);
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText(line.slice(0, 5));
}

const played = async (page: Page) => Number(await page.getByTestId("tb-sound").getAttribute("data-played"));
const peak = async (page: Page) => Number(await page.getByTestId("tb-sound").getAttribute("data-peak"));

test("Sound plays the sound card: a note set up from the monitor reaches the speaker", async ({ page }) => {
  await booted(page);
  const sound = page.getByTestId("tb-sound");
  await expect(sound).toBeEnabled();
  await expect(sound).toHaveAttribute("aria-pressed", "false");
  await sound.click();
  await expect(sound).toHaveAttribute("aria-pressed", "true");
  // The worklet plays (silence, so far): its count of samples played climbs.
  await expect.poll(() => played(page), { timeout: 10_000 }).toBeGreaterThan(4_800);
  expect(await peak(page)).toBeLessThan(0.01);
  // SID A: about 245 Hz, a triangle, attack 0, sustain F; then the volume.
  await monitor(page, "C500: 00 10 00 00 11 00 F0");
  await monitor(page, "C518: 0F");
  await expect.poll(() => peak(page), { timeout: 5_000 }).toBeGreaterThan(0.1); // 1/8 at full
  const before = await played(page);
  await expect.poll(() => played(page), { timeout: 5_000 }).toBeGreaterThan(before + 4_800);
  // Off: the port stops making samples, the meter drops.
  await sound.click();
  await expect(sound).toHaveAttribute("aria-pressed", "false");
  await expect.poll(() => peak(page)).toBe(0);
});

test("left on, Sound comes back on after a reload, and plays from the first click", async ({ page }) => {
  await booted(page);
  await page.getByTestId("tb-sound").click();
  await expect(page.getByTestId("tb-sound")).toHaveAttribute("aria-pressed", "true");
  await page.reload();
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await expect(page.getByTestId("tb-sound")).toHaveAttribute("aria-pressed", "true");
  await page.getByTestId("crt").click(); // the gesture a browser waits for
  await expect.poll(() => played(page), { timeout: 10_000 }).toBeGreaterThan(4_800);
});
