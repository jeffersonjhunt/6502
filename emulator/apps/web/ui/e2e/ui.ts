import { expect, type Page } from "@playwright/test";

// The drawer's two panels, and the controls that live in them: the
// debugger's tabs, and Settings' media rows and phosphor. And the keyswitch,
// which the page starts at OFF.

/**
 * Turn the key to RUN, once the core is open (the strip says POWER OFF): the page starts the machine off, and a
 * cold start is the visitor's turn of the key.
 */
export async function powerOn(page: Page): Promise<void> {
  const key = page.getByTestId("keyswitch");
  await expect(page.getByTestId("status")).toHaveText("POWER OFF", { timeout: 15_000 });
  await expect(key).toHaveAttribute("data-pos", "off");
  await key.click();
  await expect(key).toHaveAttribute("data-pos", "run");
}

/** Show the debugger (it starts closed on a first visit). */
export async function openDebugger(page: Page): Promise<void> {
  const button = page.getByTestId("tb-debugger");
  if ((await button.getAttribute("aria-pressed")) !== "true") await button.click();
  await expect(page.getByTestId("debugger")).toBeVisible();
}

/** Show Settings. */
export async function openSettings(page: Page): Promise<void> {
  const button = page.getByTestId("tb-settings");
  if ((await button.getAttribute("aria-pressed")) !== "true") await button.click();
  await expect(page.getByTestId("settings")).toBeVisible();
}

/** Pick a phosphor in Settings, Display, and close it again. */
export async function phosphor(page: Page, name: "Colour" | "P1" | "P3" | "P4"): Promise<void> {
  await openSettings(page);
  await page.getByTestId("settings").getByRole("button", { name, exact: true }).click();
  await page.getByTestId("settings-close").click();
}

/**
 * Wait out the boot splash and its tune: at power-on the BIOS (splash.s) has the video card cover the screen for
 * 5 s and the sound card play its tune, which stops the card's 6510 itself ($DE02). Right after the greeting, the
 * Devices tab shows both going, then both over, as test_bios waits for them; the drawer is left as it was.
 */
export async function pastSplash(page: Page): Promise<void> {
  const wasOpen = (await page.getByTestId("tb-debugger").getAttribute("aria-pressed")) === "true";
  await openDebugger(page);
  await page.locator('[data-tab="dev"]').click();
  const splash = page.getByTestId("dev-video").locator('[data-reg="Splash"]');
  const sound = page.getByTestId("dev-sound").locator('[data-reg="CTRL"]');
  await expect(splash).toContainText("playing"); // the splash covers the screen...
  await expect(sound).toContainText("6510 running"); // ...and the tune plays
  await expect(splash).toHaveText("$00  off", { timeout: 20_000 });
  await expect(sound).toContainText("6510 stopped", { timeout: 20_000 });
  if (!wasOpen) await page.getByTestId("tb-debugger").click();
  await expect(page.getByTestId("debugger")).toBeVisible({ visible: wasOpen });
}

/**
 * Pick Terminal by hand, for a test that types at the monitor or reads the serial line's screen. The page boots to
 * Display, where the keys are the USB card's: it makes characters from the keys pressed and Shift, so Playwright's
 * ":" (the ; key without Shift) arrives as ";". A pick holds until the machine next switches, and the BIOS's screen
 * doesn't switch it.
 */
export async function onTerminal(page: Page): Promise<void> {
  await page.getByRole("button", { name: "Terminal" }).click();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "text");
  await expect(page.getByTestId("crt")).toBeFocused();
}
