import { expect, test, type Page } from "@playwright/test";

// The north bridge's real-time clock on the Devices tab: set from this
// computer's clock when the page opened, and counting.

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
}

test("the Devices tab shows the clock, set from this computer's, and it ticks", async ({ page }) => {
  await booted(page);
  await page.locator('[data-tab="dev"]').click();
  const rtc = page.getByTestId("dev-rtc");
  const time = rtc.locator('[data-reg="Time"]');
  await expect(time).toHaveText(/^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d · \w+day$/);
  await expect(time).toContainText(String(new Date().getFullYear()));
  const first = await time.textContent();
  await expect(time).not.toHaveText(first!, { timeout: 5_000 }); // a second on
  await expect(rtc.locator('[data-reg="RTCCTL"]')).toContainText("TICK");
  await expect(rtc.locator('[data-reg="Control"]')).toHaveText("$00  square wave 1 Hz"); // as the bridge sets it up
  await expect(rtc.locator('[data-reg="Aging"]')).toHaveText("$00 · 25.00 °C");
});
