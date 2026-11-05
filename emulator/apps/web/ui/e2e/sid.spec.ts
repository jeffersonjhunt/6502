import { expect, test } from "@playwright/test";
import { openDebugger, openSettings, powerOn } from "./ui";
import { fakeSid } from "../src/port/fake-sid";

const GREETING = "BACKPLANE BIOS 0.10";
const peak = async (page: import("@playwright/test").Page) => Number(await page.getByTestId("tb-sound").getAttribute("data-peak"));

test("Play .sid: a tune of our own onto the card, Sound on, heard, songs, Stop", async ({ page }) => {
  await page.goto("./?sd=empty");
  await powerOn(page);
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  // The splash's tune stopped first, every voice down to 0: a tune chosen while it holds a note at $FF starts
  // frozen at 0 (its attack wraps). Only this WebUI button can come in that early, never a program after the boot.
  // Pause/Break on the USB keyboard: the BIOS's QUIET stops the card and turns its gates off.
  await openDebugger(page);
  await page.locator('[data-tab="dev"]').click();
  await page.getByTestId("crt").click();
  await page.keyboard.press("Pause");
  const sound = page.getByTestId("dev-sound");
  await expect(sound.locator('[data-reg="CTRL"]')).toContainText("6510 stopped", { timeout: 20_000 });
  for (const voice of ["A1", "A2", "A3", "B1", "B2", "B3"])
    await expect(sound.locator(`[data-reg="${voice}"]`)).toContainText("held at 0", { timeout: 20_000 });
  await page.getByTestId("tb-debugger").click();
  await expect(page.getByTestId("debugger")).toBeHidden();
  await expect(page.getByTestId("tb-sound")).toHaveAttribute("aria-pressed", "false");
  await openSettings(page);
  const chooser = page.waitForEvent("filechooser");
  await page.getByTestId("sid-play").click(); // Sound goes on in this click
  await (await chooser).setFiles({ name: "tone.sid", mimeType: "application/octet-stream", buffer: Buffer.from(fakeSid({ songs: 2 })) });
  await expect(page.getByTestId("sid-title")).toHaveText("Test Tone — OO-1 tests (2026)");
  await expect(page.getByTestId("sid-song")).toHaveText("1/2");
  await expect(page.getByTestId("sid-pill")).toBeVisible(); // now playing, in the strip
  await expect(page.getByTestId("tb-sound")).toHaveAttribute("aria-pressed", "true"); // choosing a tune turns Sound on
  await expect.poll(() => peak(page), { timeout: 5_000 }).toBeGreaterThan(0.1);
  await page.getByTestId("sid-next").click();
  await expect(page.getByTestId("sid-song")).toHaveText("2/2");
  await expect(page.getByTestId("sid-next")).toBeDisabled();
  // The Devices tab sees the card's 6510 running the tune.
  await openDebugger(page);
  await page.locator('[data-tab="dev"]').click();
  await expect(page.getByTestId("dev-sound")).toContainText("running");
  await page.getByTestId("sid-stop").click();
  await expect(page.getByTestId("sid-pill")).toBeHidden();
  await expect.poll(() => peak(page), { timeout: 5_000 }).toBeLessThan(0.01);
  await page.screenshot({ path: `test-results/sid-${test.info().project.name}.png` });
});

test("a file that isn't a tune is refused, and says why", async ({ page }) => {
  await page.goto("./?sd=empty");
  await powerOn(page);
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await page.getByTestId("sid-file").setInputFiles({ name: "notes.txt", mimeType: "text/plain", buffer: Buffer.from("hello") });
  await expect(page.getByTestId("sid-note")).toHaveText("notes.txt: not a .sid file (no PSID or RSID header)");
  await expect(page.getByTestId("sid-title")).toBeHidden();
});
