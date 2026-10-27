import { expect, test, type Page } from "@playwright/test";
import { openSettings, phosphor } from "./ui";

const GREETING = "BACKPLANE BIOS 0.9";

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await expect(page.getByTestId("status")).toContainText("RUNNING");
}

test("boots the BIOS to the Woz Monitor on the CRT", async ({ page }) => {
  await booted(page);
  await expect(page.getByTestId("crt-text")).toHaveText(/BACKPLANE BIOS 0\.9\n\\$/);
  await page.screenshot({ path: `test-results/boot-${test.info().project.name}.png` });
});

test("typing reaches the monitor: CAPS on, Enter is CR", async ({ page }) => {
  await booted(page);
  await page.getByTestId("crt").click();
  await page.keyboard.type("c680.c684");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText("C680: 00 01 02 7F 7F");
  await page.screenshot({ path: `test-results/monitor-${test.info().project.name}.png` });
});

test("Halt holds the CPU, Run lets it go: a key typed while halted echoes after", async ({ page }) => {
  await booted(page);
  await page.getByTestId("tb-halt").click();
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await page.getByTestId("crt").click();
  await page.keyboard.type("Q");
  await page.waitForTimeout(600);
  await expect(page.getByTestId("crt-text")).not.toContainText("Q");
  await page.getByTestId("tb-step").click(); // one instruction: still halted
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await page.getByTestId("tb-run").click();
  await expect(page.getByTestId("status")).toContainText("RUNNING");
  await expect(page.getByTestId("crt-text")).toContainText("Q");
});

test("the whole screen stays inside the glass, typing line included, after it scrolls", async ({ page }) => {
  await booted(page);
  await page.getByTestId("crt").click();
  for (let i = 0; i < 16; i++) {
    await page.keyboard.type("C680.C684");
    await page.keyboard.press("Enter");
  }
  await page.keyboard.type("LAST");
  await expect(page.getByTestId("crt-text")).toContainText("LAST");
  const box = await page.evaluate(() => {
    const glass = document.querySelector("[data-testid=crt]")!.getBoundingClientRect();
    const screen = document.querySelector("[data-testid=crt] canvas")!.getBoundingClientRect();
    return { glass: [glass.left, glass.top, glass.right, glass.bottom], screen: [screen.left, screen.top, screen.right, screen.bottom] };
  });
  expect(box.screen[0]).toBeGreaterThanOrEqual(box.glass[0]);
  expect(box.screen[1]).toBeGreaterThanOrEqual(box.glass[1]);
  expect(box.screen[2]).toBeLessThanOrEqual(box.glass[2]);
  expect(box.screen[3]).toBeLessThanOrEqual(box.glass[3]);
  const [l, t, r, b] = box.screen;
  expect((r - l) / (b - t)).toBeCloseTo(4 / 3, 2); // not stretched
  await page.screenshot({ path: `test-results/scrolled-${test.info().project.name}.png` });
});

test("the CRT is never scaled up: the canvas has at least the pixels the display shows", async ({ page }) => {
  await booted(page);
  for (const width of [900, 1440, 1920]) {
    await page.setViewportSize({ width, height: Math.round(width * 0.6) });
    await expect
      .poll(() =>
        page.evaluate(() => {
          const c = document.querySelector<HTMLCanvasElement>("[data-testid=crt] canvas")!;
          return c.width / (c.getBoundingClientRect().width * devicePixelRatio);
        }),
      )
      .toBeGreaterThanOrEqual(0.95);
  }
  await page.screenshot({ path: `test-results/sharp-${test.info().project.name}.png` });
});

test("phosphor sets the glow and is remembered", async ({ page }) => {
  await booted(page);
  await phosphor(page, "P1");
  const halo = () => page.evaluate(() => document.documentElement.style.getPropertyValue("--halo"));
  expect(await halo()).toBe("#33ff6614");
  await page.reload();
  await openSettings(page);
  await expect(page.getByRole("button", { name: "P1" })).toHaveAttribute("aria-pressed", "true");
  expect(await halo()).toBe("#33ff6614");
});

test("Display is the video card, Terminal the serial line; the monitor says which", async ({ page }) => {
  await booted(page);
  const label = page.getByTestId("mon-label");
  await expect(label).toHaveText("Terminal · the serial line");
  await expect(label).toHaveCSS("text-transform", "uppercase");
  await page.getByRole("button", { name: "Display" }).click();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "vga"); // not in Apple II mode: VGA
  await expect(label).toHaveText("Display · VGA 640×480");
  await expect(page.getByRole("button", { name: "Display" })).toHaveAttribute("aria-pressed", "true");
  await page.getByRole("button", { name: "Terminal" }).click();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "text");
  await expect(page.getByTestId("crt")).toBeFocused(); // the keyboard back on the screen
  await expect(page.getByTestId("crt-text")).toContainText(GREETING);
});

test("Step cycle is disabled: the emulator steps whole instructions", async ({ page }) => {
  await booted(page);
  await expect(page.getByRole("button", { name: "Step cycle" })).toBeDisabled();
});

test("a missing core says so instead of a blank screen", async ({ page }) => {
  await page.route("**/core/oo1.mjs?*", (route) => route.fulfill({ status: 404, body: "" })); // ?v=<version>
  await page.goto("./");
  await expect(page.getByTestId("status")).toHaveText("NO MACHINE");
  await expect(page.getByTestId("crt-text")).not.toBeEmpty();
});

test("a missing wasm says so too", async ({ page }) => {
  await page.route("**/core/oo1.wasm?*", (route) => route.fulfill({ status: 404, body: "" }));
  await page.goto("./");
  await expect(page.getByTestId("status")).toHaveText("NO MACHINE");
});

test("Ctrl+V and Shift+Insert paste, on the serial terminal and on the USB keyboard", async ({ page, context, browserName }) => {
  test.skip(browserName !== "chromium", "the clipboard is granted to Chromium only");
  await context.grantPermissions(["clipboard-read", "clipboard-write"]);
  await booted(page);
  const clip = (text: string) => page.evaluate((t) => navigator.clipboard.writeText(t), text);
  await page.getByTestId("crt").click();
  await clip("c680.c684\n");
  await page.keyboard.press("Control+V");
  await expect(page.getByTestId("crt-text")).toContainText("C680: 00 01 02 7F 7F");
  await clip("c681\n");
  await page.keyboard.press("Shift+Insert");
  await expect(page.getByTestId("crt-text")).toContainText("C681: 01");
  await page.getByRole("button", { name: "Display" }).click();
  await page.getByTestId("crt").click();
  await clip("c682\n");
  await page.keyboard.press("Control+V");
  await expect(page.getByTestId("crt-text")).toContainText("C682: 02");
  await clip("c683\n");
  await page.keyboard.press("Shift+Insert");
  await expect(page.getByTestId("crt-text")).toContainText("C683: 7F");
});

test("F6 takes the keyboard off the screen to what follows it, Shift+F6 to what comes before; the machine gets neither", async ({ page }) => {
  await booted(page);
  const crt = page.getByTestId("crt");
  await expect(crt).toHaveAttribute("aria-label", /F6 leaves the screen/);
  await crt.click();
  await page.keyboard.press("F6");
  await expect(crt).not.toBeFocused();
  expect(await page.evaluate(() => document.activeElement?.tagName)).toBe("BUTTON");
  await page.keyboard.press("Shift+Tab"); // the screen is the stop before it
  await expect(crt).toBeFocused();
  await page.keyboard.press("Shift+F6");
  await expect(crt).not.toBeFocused();
  await page.keyboard.press("Tab");
  await expect(crt).toBeFocused();
  await page.keyboard.type("c680");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText(/\\\nC680\n+C680: 00/); // nothing typed before it
  await page.getByRole("button", { name: "Display" }).click();
  await expect(crt).toHaveAttribute("aria-label", /F6 leaves the screen/);
  await crt.click();
  await page.keyboard.press("F6");
  await expect(crt).not.toBeFocused();
});
