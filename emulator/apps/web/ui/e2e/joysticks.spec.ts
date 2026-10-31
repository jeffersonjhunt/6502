import { expect, test, type Page } from "@playwright/test";
import { openSettings } from "./ui";

// The serial card's joysticks from the page: a mocked Gamepad API (the
// browser's own has no pads in a headless run) and the Keys switch, read
// back by the Woz Monitor at $C340 (joystick 2) and $C341 (joystick 1).

const GREETING = "BACKPLANE BIOS 0.9";

type Pad = { index: number; id: string; connected: boolean; mapping: string; axes: number[]; buttons: { pressed: boolean }[] };

async function booted(page: Page) {
  await page.addInitScript(() => {
    const w = window as unknown as { pads: (Pad | null)[] };
    w.pads = [];
    Object.defineProperty(navigator, "getGamepads", { configurable: true, value: () => w.pads });
  });
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
}

/** A pad in the browser's list at `index`, `pressed` its buttons. */
async function plug(page: Page, index: number, id: string, pressed: number[]) {
  await page.evaluate(
    ([i, name, down]) => {
      const w = window as unknown as { pads: (Pad | null)[] };
      w.pads[i as number] = {
        index: i as number,
        id: name as string,
        connected: true,
        mapping: "standard",
        axes: [0, 0, 0, 0],
        buttons: Array.from({ length: 17 }, (_, b) => ({ pressed: (down as number[]).includes(b) })),
      };
    },
    [index, id, pressed] as const,
  );
}

const screen = (page: Page) => page.getByRole("application", { name: "Terminal: click and type" });

async function readSticks(page: Page, want: string) {
  await page.keyboard.type("C340.C341");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("crt-text")).toContainText(`C340: ${want}`);
}

test("a gamepad is joystick 1: Settings names it, the strip shows it, the monitor reads it", async ({ page }) => {
  await booted(page);
  await expect(page.getByTestId("pill-joy")).toBeHidden();
  await plug(page, 0, "Test Pad (STANDARD GAMEPAD Vendor: 0000 Product: 0000)", [12, 0]); // d-pad up, A
  await expect(page.getByTestId("pill-joy")).toHaveText("JOY 1");
  await page.getByTestId("pill-joy").click();
  await expect(page.getByTestId("joy1-meta")).toHaveText("Gamepad: Test Pad");
  await expect(page.getByTestId("joy2-meta")).toHaveText("No gamepad");
  await expect(page.getByTestId("joy1-light")).toHaveClass(/on/);
  await screen(page).click();
  await readSticks(page, "FF EE"); // joystick 2 untouched; 1: up and fire, 0
});

test("Keys, off by default, make the arrows and Space joystick 1 and W A S D joystick 2, and leave the rest to the terminal", async ({ page }) => {
  await booted(page);
  await openSettings(page);
  await expect(page.getByTestId("joy-keys")).toHaveAttribute("aria-pressed", "false");
  await page.getByTestId("joy-keys").click();
  await expect(page.getByTestId("joy-keys")).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByTestId("pill-joy")).toHaveText("JOY 1 2");
  await screen(page).click();
  await page.keyboard.down("ArrowRight");
  await page.keyboard.down("KeyW"); // joystick 2 up
  await readSticks(page, "FE F7");
  await page.keyboard.up("ArrowRight");
  await page.keyboard.up("KeyW");
  await readSticks(page, "FF FF");
  // Remembered: still on after a reload.
  await page.reload();
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await expect(page.getByTestId("pill-joy")).toHaveText("JOY 1 2");
});
