import { expect, test, type Page } from "@playwright/test";

const GREETING = "BACKPLANE BIOS 0.1";

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await expect(page.getByTestId("status")).toContainText("RUNNING");
}

test("boots the BIOS to the Woz Monitor on the CRT", async ({ page }) => {
  await booted(page);
  await expect(page.getByTestId("crt-text")).toHaveText(/BACKPLANE BIOS 0\.1\n\\$/);
  await page.screenshot({ path: "test-results/boot.png" });
});

test("typing reaches the monitor: CAPS on, Enter is CR", async ({ page }) => {
  await booted(page);
  await page.getByTestId("crt").click();
  await page.keyboard.type("c680.c684");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText("C680: 00 01 02 7F 7F");
  await page.screenshot({ path: "test-results/monitor.png" });
});

test("Halt holds the CPU, Run lets it go: a key typed while halted echoes after", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "Halt" }).click();
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await page.getByTestId("crt").click();
  await page.keyboard.type("Q");
  await page.waitForTimeout(600);
  await expect(page.getByTestId("crt-text")).not.toContainText("Q");
  await page.getByRole("button", { name: "Step inst" }).click(); // one instruction: still halted
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await page.getByRole("button", { name: "Run" }).click();
  await expect(page.getByTestId("status")).toContainText("RUNNING");
  await expect(page.getByTestId("crt-text")).toContainText("Q");
});

test("phosphor sets the glow and is remembered", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "P1" }).click();
  const halo = () => page.evaluate(() => document.documentElement.style.getPropertyValue("--halo"));
  expect(await halo()).toBe("#33ff6614");
  await page.reload();
  await expect(page.getByRole("button", { name: "P1" })).toHaveAttribute("aria-pressed", "true");
  expect(await halo()).toBe("#33ff6614");
});

test("Tiles and Apple II show NO SIGNAL until the video card exists", async ({ page }) => {
  await booted(page);
  await page.getByRole("button", { name: "Tiles" }).click();
  await expect(page.getByTestId("crt")).toHaveAttribute("data-scene", "tiles");
  // The middle of the screen is lit (the NO SIGNAL caption); text mode's middle is dark.
  const lit = () => page.evaluate(() => {
    const c = document.querySelector<HTMLCanvasElement>("[data-testid=crt] canvas")!;
    const k = c.width / 640; // the canvas holds more pixels than VGA's 640x480
    const d = c.getContext("2d")!.getImageData(200 * k, 210 * k, 240 * k, 40 * k).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 200) n++;
    return n;
  });
  await expect.poll(lit).toBeGreaterThan(50); // drawn on the next animation frame
  await page.screenshot({ path: "test-results/no-signal.png" });
  await page.getByRole("button", { name: "Text" }).click();
  await expect(page.getByTestId("crt-text")).toContainText(GREETING);
});

test("Step cycle is disabled: the emulator steps whole instructions", async ({ page }) => {
  await booted(page);
  await expect(page.getByRole("button", { name: "Step cycle" })).toBeDisabled();
});

test("a missing core says so instead of a blank screen", async ({ page }) => {
  await page.route("**/core/oo1.mjs", (route) => route.fulfill({ status: 404, body: "" }));
  await page.goto("./");
  await expect(page.getByTestId("status")).toHaveText("NO MACHINE");
  await expect(page.getByTestId("crt-text")).not.toBeEmpty();
});
