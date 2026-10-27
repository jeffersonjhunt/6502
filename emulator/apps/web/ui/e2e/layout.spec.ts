import { expect, test, type Page } from "@playwright/test";
import { openSettings } from "./ui";

// The page's layout (the approved redesign): one toolbar row, one drawer
// for Settings and the debugger, the status strip, the monitor following
// the machine, a BIOS of your own, the bundled fonts, and a phone's width.

const GREETING = "BACKPLANE BIOS 0.9";

async function booted(page: Page, greeting = GREETING) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(greeting, { timeout: 15_000 });
}

test("a first visit: the drawer closed; Settings and the debugger share it, one at a time; Escape closes it", async ({ page }) => {
  await booted(page);
  await expect(page.getByTestId("drawer")).toBeHidden();
  await expect(page.getByTestId("tb-debugger")).toHaveAttribute("aria-pressed", "false");
  await page.getByTestId("tb-settings").click();
  const settings = page.getByRole("region", { name: "Settings" });
  await expect(settings).toBeVisible();
  await expect(settings).toBeFocused(); // focus goes in
  await expect(page.getByTestId("tb-settings")).toHaveAttribute("aria-pressed", "true");
  await page.getByTestId("tb-debugger").click();
  await expect(settings).toBeHidden();
  await expect(page.getByRole("region", { name: "Debugger" })).toBeVisible();
  await expect(page.getByTestId("tb-settings")).toHaveAttribute("aria-pressed", "false");
  await page.locator('[data-tab="trace"]').click();
  await page.keyboard.press("Escape");
  await expect(page.getByTestId("drawer")).toBeHidden();
  await expect(page.getByTestId("tb-debugger")).toBeFocused(); // back to its button
  // In the toolbar's one row: Sound, beside CAPS; the phosphor is in Settings now.
  const top = async (id: string) => (await page.getByTestId(id).boundingBox())!.y;
  expect(await top("tb-sound")).toBe(await top("tb-run"));
  expect(await top("tb-debugger")).toBe(await top("tb-run"));
  await expect(page.getByRole("banner").getByRole("group", { name: "Phosphor" })).toHaveCount(0);
});

test("the strip: nothing loaded says so, and its link opens Settings at the ROMs", async ({ page }) => {
  await booted(page);
  const strip = page.getByRole("region", { name: "Machine status and media" });
  await expect(strip).toContainText("OO-1 · VGA 640×480 · RUNNING");
  await expect(strip.locator(".strip-text")).toHaveText(/^OO-1 · VGA 640×480 · RUNNING · \d+\.\d\d MHz$/, { useInnerText: true }); // as rendered: MHz
  await expect(page.getByTestId("no-media")).toHaveText("No media loaded. Load in Settings");
  await page.getByTestId("load-in-settings").click();
  await expect(page.getByTestId("settings")).toBeVisible();
  await expect(page.locator("#set-roms")).toBeFocused();
  // Drive 2 too: its own pill.
  await page.getByTestId("disk-file-2").setInputFiles({ name: "Two.dsk", mimeType: "application/octet-stream", buffer: Buffer.alloc(143_360) });
  await expect(page.getByTestId("pill-d2")).toHaveText("D2 Two.dsk");
  await expect(page.getByTestId("no-media")).toBeHidden();
  await page.getByTestId("disk-eject-2").click();
  await expect(page.getByTestId("no-media")).toBeVisible();
});

test("Settings opened from the strip or the page: closing it puts focus back on what opened it", async ({ page }) => {
  await booted(page);
  for (const id of ["load-in-settings", "about-link"]) {
    await page.getByTestId(id).click();
    await expect(page.getByTestId("settings")).toBeVisible();
    await page.keyboard.press("Escape");
    await expect(page.getByTestId(id)).toBeFocused();
  }
  await page.getByTestId("disk-file").setInputFiles({ name: "A.dsk", mimeType: "application/octet-stream", buffer: Buffer.alloc(143_360) });
  await page.getByTestId("pill-d1").click();
  await page.getByTestId("settings-close").click();
  await expect(page.getByTestId("pill-d1")).toBeFocused();
});

test("Settings' buttons each name their row: no two alike for a screen reader", async ({ page }) => {
  await booted(page);
  await openSettings(page);
  for (const id of ["disk-file", "disk-file-2"]) await page.getByTestId(id).setInputFiles({ name: "A.dsk", mimeType: "application/octet-stream", buffer: Buffer.alloc(143_360) });
  await expect(page.getByTestId("disk-eject-2")).toBeVisible();
  const names = await page.getByTestId("settings").getByRole("button").evaluateAll((bs) =>
    bs.filter((b) => b.getClientRects().length).map((b) => b.getAttribute("aria-label") ?? b.textContent!.trim()),
  );
  expect(names.filter((n, i) => names.indexOf(n) !== i)).toEqual([]);
  expect(names).toEqual(expect.arrayContaining(["Load the IIe character ROM", "Eject drive 2", "Save drive 1's disk", "Protect drive 2's disk", "Load a BIOS of your own"]));
});

test("the monitor follows the machine: the demo's picture switches to Display, its end back to Terminal; a choice by hand holds", async ({ page }) => {
  await booted(page);
  const crt = page.getByTestId("crt");
  await expect(crt).toHaveAttribute("data-scene", "text"); // the BIOS's own screen on the card is the terminal's
  await crt.click();
  await page.keyboard.type("C803R");
  await page.keyboard.press("Enter");
  await expect(crt).toHaveAttribute("data-scene", "vga", { timeout: 10_000 }); // by itself
  await expect(page.getByTestId("mon-label")).toHaveText("DISPLAY · VGA 640×480");
  await expect(page.getByRole("button", { name: "Display" })).toHaveAttribute("aria-pressed", "true");
  await page.getByRole("button", { name: "Terminal" }).click(); // by hand, while the demo runs
  await page.waitForTimeout(1000);
  await expect(crt).toHaveAttribute("data-scene", "text"); // it holds
  await page.keyboard.press("x"); // the demo ends: the machine is back on the BIOS's screen
  await expect(page.getByTestId("crt-text")).toContainText("C803: 4C\\", { timeout: 10_000 });
  await page.keyboard.type("C803R");
  await page.keyboard.press("Enter");
  await expect(crt).toHaveAttribute("data-scene", "vga", { timeout: 10_000 }); // a change again: followed
  await page.keyboard.press("x");
  await expect(crt).toHaveAttribute("data-scene", "text", { timeout: 10_000 });
});

/** The core's own bios.rom, as the page loads it (bios-labels.ts reads its labels the same way). */
async function builtInBios(page: Page): Promise<Buffer> {
  const bytes = await page.evaluate(async () => {
    const entry = performance.getEntriesByType("resource").find((e) => /\/core\/oo1\.mjs(\?|$)/.test(e.name))!;
    const url = new URL(entry.name);
    const at = (file: string) => {
      const u = new URL(file, url);
      u.search = url.search;
      return u.href;
    };
    type Core = { FS: { readFile(path: string): Uint8Array } };
    const mod = (await import(/* @vite-ignore */ url.href)) as { default: (o: { locateFile: (f: string) => string }) => Promise<Core> };
    return Array.from((await mod.default({ locateFile: at })).FS.readFile("/roms/bios.rom"));
  });
  return Buffer.from(bytes);
}

test("a BIOS of your own: the machine restarts on it, it is kept, and Use built-in goes back", async ({ page }) => {
  await booted(page);
  await openSettings(page);
  await expect(page.getByTestId("bios-meta")).toHaveText("built-in 0.9 · or load your own build");
  const rom = await builtInBios(page);
  const at = rom.indexOf("BACKPLANE BIOS 0.9") + "BACKPLANE BIOS ".length;
  rom.write("7.7", at, "latin1"); // a build of its own
  await page.getByTestId("bios-file").setInputFiles({ name: "my-bios.rom", mimeType: "application/octet-stream", buffer: rom });
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 7.7", { timeout: 15_000 }); // the page restarted on it
  await openSettings(page);
  await expect(page.getByTestId("bios-meta")).toHaveText("yours: my-bios.rom · 7.7 · kept in this browser");
  await page.reload();
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 7.7", { timeout: 15_000 });
  await openSettings(page);
  await page.getByTestId("bios-builtin").click();
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  expect(await page.evaluate(() => localStorage.getItem("oo1.bios"))).toBeNull();
});

test("a BIOS that runs nothing useful: Settings still takes you back to the page's own", async ({ page }) => {
  await booted(page);
  await openSettings(page);
  await page.getByTestId("bios-file").setInputFiles({ name: "blank.rom", mimeType: "application/octet-stream", buffer: Buffer.alloc(4096, 0xff) });
  await expect(page.getByTestId("status")).toContainText(/RUNNING|HALTED|STOPPED/, { timeout: 15_000 }); // restarted, on it
  await page.waitForTimeout(500);
  await expect(page.getByTestId("crt-text")).not.toContainText("BACKPLANE BIOS");
  // Silent: the strip points the way back.
  await expect(page.getByTestId("bios-quiet")).toHaveText("Your BIOS, blank.rom, has printed nothing: Settings › ROMs › Use built-in", { timeout: 10_000 });
  await page.getByTestId("bios-quiet-open").click();
  await expect(page.locator("#set-roms")).toBeFocused();
  await page.keyboard.press("Escape");
  await openSettings(page);
  await expect(page.getByTestId("bios-meta")).toHaveText("yours: blank.rom · kept in this browser"); // no version to read from it
  await page.getByTestId("bios-builtin").click();
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
});

test("the fonts are the page's own: Inter and JetBrains Mono load, and nothing is fetched from elsewhere", async ({ page, baseURL }) => {
  const outside: string[] = [];
  page.on("request", (r) => {
    if (new URL(r.url()).origin !== new URL(baseURL!).origin && !r.url().startsWith("data:")) outside.push(r.url());
  });
  await booted(page);
  await page.getByTestId("tb-debugger").click(); // the registers are JetBrains Mono
  const loaded = await page.evaluate(async () => {
    await document.fonts.ready;
    return ["400 13px Inter", "600 10px Inter", "800 17px Inter", "400 12px 'JetBrains Mono'", "600 14px 'JetBrains Mono'"].map((f) => document.fonts.check(f));
  });
  expect(loaded).toEqual([true, true, true, true, true]);
  const faces = await page.evaluate(() => [...document.fonts].filter((f) => f.status === "loaded").map((f) => `${f.family} ${f.weight}`));
  expect(faces).toEqual(expect.arrayContaining(["Inter 600", "JetBrains Mono 600"]));
  expect(outside).toEqual([]);
});

test.describe("on a narrow window", () => {
  test.use({ viewport: { width: 800, height: 900 } });

  test("the drawer over the page is a modal dialog, the page behind it inert; the debugger left open stays closed", async ({ page }) => {
    await booted(page);
    await page.getByTestId("tb-settings").click();
    const settings = page.getByRole("dialog", { name: "Settings" });
    await expect(settings).toBeFocused();
    expect(await page.locator(".bench").evaluate((b) => (b as HTMLElement).inert)).toBe(true);
    await page.keyboard.press("Escape");
    expect(await page.locator(".bench").evaluate((b) => (b as HTMLElement).inert)).toBe(false);
    await page.getByTestId("tb-debugger").click();
    await page.reload();
    await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
    await expect(page.getByTestId("drawer")).toBeHidden();
  });
});

test.describe("on a phone", () => {
  test.use({ viewport: { width: 390, height: 844 } });

  test("one header row, the panel scaled to fit with no sideways scroll, More holds the rest, touch-sized targets", async ({ page }) => {
    await booted(page);
    const header = page.getByRole("banner");
    expect((await header.boundingBox())!.height).toBeLessThan(70); // one row
    for (const id of ["tb-run", "tb-step"]) await expect(page.getByTestId(id)).toBeHidden(); // Run: the machine runs; Step: under More
    await expect(page.getByTestId("tb-halt")).toBeVisible();
    expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
    const rack = (await page.locator(".rack").boundingBox())!;
    expect(rack.x).toBeGreaterThanOrEqual(0);
    expect(rack.x + rack.width).toBeLessThanOrEqual(390);
    expect(Number(await page.getByTestId("panel-fit").getAttribute("data-scale"))).toBeLessThan(1);
    const small = await page.evaluate(() =>
      [...document.querySelectorAll<HTMLElement>("header button, header a, .strip button, .mon-foot button")]
        .filter((b) => b.getClientRects().length)
        .map((b) => b.getBoundingClientRect())
        .filter((r) => r.width < 44 || r.height < 44).length,
    );
    expect(small).toBe(0);
    await page.getByTestId("tb-more").click();
    await expect(page.getByTestId("tb-menu")).toBeVisible();
    await expect(page.getByTestId("tb-menu").getByRole("group", { name: "Clock" })).toBeVisible();
    await page.getByTestId("tb-menu").getByRole("button", { name: "Debugger" }).click();
    const dbg = page.getByRole("dialog", { name: "Debugger" });
    await expect(dbg).toBeVisible();
    expect((await dbg.boundingBox())!.width).toBe(390); // the whole width, over the page
    await page.keyboard.press("Escape");
    await expect(dbg).toBeHidden();
    await expect(page.getByTestId("tb-more")).toBeFocused(); // its button is folded away: More stands in
    await page.screenshot({ path: `test-results/phone-${test.info().project.name}.png`, fullPage: true });
  });
});
