import { expect, test } from "@playwright/test";
import { fakeSid } from "../src/port/fake-sid";

const GREETING = "BACKPLANE BIOS 0.9";
const peak = async (page: import("@playwright/test").Page) => Number(await page.getByTestId("tb-sound").getAttribute("data-peak"));

test("Play .sid: a tune of our own onto the card, Sound on, heard, songs, Stop", async ({ page }) => {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await expect(page.getByTestId("tb-sound")).toHaveAttribute("aria-pressed", "false");
  await page.getByTestId("sid-file").setInputFiles({ name: "tone.sid", mimeType: "application/octet-stream", buffer: Buffer.from(fakeSid({ songs: 2 })) });
  await expect(page.getByTestId("sid-title")).toHaveText("Test Tone — OO-1 tests (2026)");
  await expect(page.getByTestId("sid-song")).toHaveText("song 1 of 2");
  await expect(page.getByTestId("tb-sound")).toHaveAttribute("aria-pressed", "true"); // choosing a tune turns Sound on
  await expect.poll(() => peak(page), { timeout: 5_000 }).toBeGreaterThan(0.1);
  await page.getByTestId("sid-next").click();
  await expect(page.getByTestId("sid-song")).toHaveText("song 2 of 2");
  await expect(page.getByTestId("sid-next")).toBeDisabled();
  // The Devices tab sees the card's 6510 running the tune.
  await page.locator('[data-tab="dev"]').click();
  await expect(page.getByTestId("dev-sound")).toContainText("running");
  await page.getByTestId("sid-stop").click();
  await expect(page.getByTestId("sid-title")).toBeHidden();
  await expect.poll(() => peak(page), { timeout: 5_000 }).toBeLessThan(0.01);
  await page.screenshot({ path: `test-results/sid-${test.info().project.name}.png` });
});

test("a file that isn't a tune is refused, and says why", async ({ page }) => {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await page.getByTestId("sid-file").setInputFiles({ name: "notes.txt", mimeType: "text/plain", buffer: Buffer.from("hello") });
  await expect(page.getByTestId("sid-note")).toHaveText("notes.txt: not a .sid file (no PSID or RSID header)");
  await expect(page.getByTestId("sid-title")).toBeHidden();
});
