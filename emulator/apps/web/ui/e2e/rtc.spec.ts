import { expect, test, type Page } from "@playwright/test";
import { openDebugger } from "./ui";

// The north bridge's real-time clock on the Devices tab: set from this
// computer's clock when the page opened, and counting.

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
}

test("the Devices tab shows the clock, set from this computer's, and it ticks", async ({ page }) => {
  test.setTimeout(45_000); // the boot's 15 s and the tick's 15 s
  const year = new Date().getFullYear(); // before booting: midnight on 31 December may pass meanwhile
  await booted(page);
  await openDebugger(page);
  await page.locator('[data-tab="dev"]').click();
  const rtc = page.getByTestId("dev-rtc");
  const time = rtc.locator('[data-reg="Time"]');
  await expect(time).toHaveText(/^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d · \w+day$/);
  await expect(time).toHaveText(new RegExp(`^(${year}|${year + 1})-`));
  const first = await time.textContent();
  await expect(time).not.toHaveText(first!, { timeout: 15_000 }); // a second on, of emulated time: a slow browser takes longer
  await expect(rtc.locator('[data-reg="RTCCTL"]')).toContainText("TICK");
  await expect(rtc.locator('[data-reg="Control"]')).toHaveText("$00  square wave 1 Hz"); // as the bridge sets it up
  await expect(rtc.locator('[data-reg="Aging"]')).toHaveText("$00 · 25.00 °C");
});
