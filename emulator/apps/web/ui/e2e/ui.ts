import { expect, type Page } from "@playwright/test";

// The drawer's two panels, and the controls that live in them: the
// debugger's tabs, and Settings' media rows and phosphor.

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
