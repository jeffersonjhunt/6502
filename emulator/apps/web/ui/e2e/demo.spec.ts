import { expect, test, type Page } from "@playwright/test";
import { onTerminal, openSettings, powerOn } from "./ui";

// The demo card: the page puts it in the SD slot at every visit, so a power-on boots OODOS from it after the
// splash. A key during the splash is the Woz Monitor instead, and so is a power-on once it's ejected. The other
// specs open the page with ?sd=empty, for the monitor.

const GREETING = "BACKPLANE BIOS 0.10";
const BOOTING = "BOOTING THE SD CARD: A KEY FOR THE MONITOR";

async function command(page: Page, line: string) {
  await page.keyboard.type(line);
  await page.keyboard.press("Enter");
}

/** Off, then on again: a cold start, the card left in. */
async function powerCycle(page: Page) {
  const key = page.getByTestId("keyswitch");
  await key.click(); // RUN to LOCK
  await key.click(); // LOCK to OFF
  await expect(key).toHaveAttribute("data-pos", "off");
  await expect(page.getByTestId("status")).toHaveText("POWER OFF");
  await key.click();
  await expect(key).toHaveAttribute("data-pos", "run");
}

test("the demo card is in at every visit, and power-on boots OODOS from it to A>", async ({ page }) => {
  await page.goto("./");
  await openSettings(page);
  await expect(page.getByTestId("sd-name")).toHaveText("oodos-demo.img", { timeout: 15_000 });
  await expect(page.getByTestId("sd-meta")).toHaveText("32 MB");
  await expect(page.getByTestId("sd-note")).toContainText("power-on boots OODOS");
  await expect(page.getByTestId("sd-insert")).toBeVisible(); // a card of your own takes its place
  await page.getByTestId("settings-close").click();
  await powerOn(page);
  await onTerminal(page);
  const crt = page.getByTestId("crt-text");
  await expect(crt).toContainText(BOOTING, { timeout: 15_000 });
  await expect(crt).toContainText("A>", { timeout: 30_000 }); // the splash's 5 s, then OOLDR and OODOS
  await command(page, "TYPE README.TXT");
  await expect(crt).toContainText("This is the OO-1's demo card");
  await expect(crt).toContainText(/with no card\.\s*A>$/); // the prompt again: a key typed while OODOS writes waits, only one
  await command(page, "HELLO");
  await expect(crt).toContainText("Hello from OODOS, a character at a time by C_WRITE");
  await page.screenshot({ path: `test-results/demo-${test.info().project.name}.png` });
});

test("a key during the splash is the Woz Monitor, the demo card still in", async ({ page }) => {
  await page.goto("./");
  await openSettings(page);
  await expect(page.getByTestId("sd-name")).toHaveText("oodos-demo.img", { timeout: 15_000 });
  await page.getByTestId("settings-close").click();
  await powerOn(page);
  const crt = page.getByTestId("crt-text");
  await expect(crt).toContainText(BOOTING, { timeout: 15_000 });
  await onTerminal(page);
  await page.keyboard.press("Space");
  await expect(crt).toContainText("\\", { timeout: 10_000 });
  await page.waitForTimeout(8_000); // past the splash: still the monitor
  await expect(crt).not.toContainText("A>");
  await command(page, "C683");
  await expect(crt).toContainText("C683: 7F"); // the monitor answers
});

test("ejected, the next power-on is the Woz Monitor; a reload puts the demo card back", async ({ page }) => {
  await page.goto("./");
  await openSettings(page);
  await expect(page.getByTestId("sd-name")).toHaveText("oodos-demo.img", { timeout: 15_000 });
  await page.getByTestId("sd-eject").click();
  await expect(page.getByTestId("sd-meta")).toHaveText("empty");
  await page.getByTestId("settings-close").click();
  await powerOn(page);
  await onTerminal(page);
  const crt = page.getByTestId("crt-text");
  await expect(crt).toContainText(GREETING, { timeout: 15_000 });
  await page.waitForTimeout(8_000); // past the splash
  await expect(crt).not.toContainText(BOOTING); // no card: it says nothing of one
  await command(page, "C683");
  await expect(crt).toContainText("C683: 7F");
  await page.reload();
  await openSettings(page);
  await expect(page.getByTestId("sd-name")).toHaveText("oodos-demo.img", { timeout: 15_000 });
});

test("WOZMON leaves OODOS for the monitor, and a power cycle boots the demo card again", async ({ page }) => {
  test.setTimeout(90_000); // two boots
  await page.goto("./");
  await openSettings(page);
  await expect(page.getByTestId("sd-name")).toHaveText("oodos-demo.img", { timeout: 15_000 });
  await page.getByTestId("settings-close").click();
  await powerOn(page);
  await onTerminal(page);
  const crt = page.getByTestId("crt-text");
  await expect(crt).toContainText("A>", { timeout: 30_000 });
  await command(page, "WOZMON");
  await expect(crt).toHaveText(/^\s*\\\s*$/); // the screen cleared, then the monitor's prompt alone
  await command(page, "C683");
  await expect(crt).toContainText("C683: 7F");
  await powerCycle(page);
  await onTerminal(page);
  await expect(crt).not.toContainText("C683", { timeout: 15_000 }); // the terminal starts afresh at power-on
  await expect(crt).toContainText(BOOTING, { timeout: 15_000 });
  await expect(crt).toContainText("A>", { timeout: 30_000 }); // booted again
});

test("a demo card that never arrives doesn't hold the page up: power-on is the Woz Monitor", async ({ page }) => {
  await page.route("**/demo-card.img.gz*", () => {}); // never answered
  await page.goto("./");
  await expect(page.getByTestId("crt")).toBeFocused({ timeout: 15_000 }); // the last of the start-up
  await openSettings(page);
  await expect(page.getByTestId("sd-meta")).toHaveText("empty");
  await page.getByTestId("settings-close").click();
  await powerOn(page);
  await onTerminal(page);
  const crt = page.getByTestId("crt-text");
  await expect(crt).toContainText(GREETING, { timeout: 15_000 });
  await page.waitForTimeout(8_000); // past the splash
  await expect(crt).not.toContainText(BOOTING);
  await command(page, "C683");
  await expect(crt).toContainText("C683: 7F");
});

test("CLEAR clears the Terminal, and CLS clears it and lists the files", async ({ page }) => {
  await page.goto("./");
  await openSettings(page);
  await expect(page.getByTestId("sd-name")).toHaveText("oodos-demo.img", { timeout: 15_000 });
  await page.getByTestId("settings-close").click();
  await powerOn(page);
  await onTerminal(page);
  const crt = page.getByTestId("crt-text");
  await expect(crt).toContainText("A>", { timeout: 30_000 });
  await command(page, "CLEAR");
  await expect(crt).toHaveText(/^\s*A>$/); // only the prompt, and no stray [ or E
  await command(page, "CLS");
  await expect(crt).toContainText("SYSTEM FILE(S) EXIST");
  await expect(crt).toHaveText(/^\s*A: README\s+TXT : OODOS\s+SYS\s+SYSTEM FILE\(S\) EXIST\s*A>$/); // from the top
});

test("?sd=empty leaves the slot empty", async ({ page }) => {
  await page.goto("./?sd=empty");
  await expect(page.getByTestId("status")).toHaveText("POWER OFF", { timeout: 15_000 });
  await openSettings(page);
  await expect(page.getByTestId("sd-bar")).toBeVisible();
  await expect(page.getByTestId("sd-meta")).toHaveText("empty");
  await expect(page.getByTestId("sd-insert")).toBeVisible();
});
