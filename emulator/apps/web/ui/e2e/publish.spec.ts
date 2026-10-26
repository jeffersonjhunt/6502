import { expect, test } from "@playwright/test";

// What publishing the page depends on: the core is fetched with the build's
// version (so a cache cannot serve an old one), and a touch screen is told
// it needs a keyboard.

test("the core and its wasm are fetched with the build's version", async ({ page }) => {
  const core: string[] = [];
  page.on("request", (r) => {
    if (r.url().includes("/core/")) core.push(r.url());
  });
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS", { timeout: 15_000 });
  const mjs = core.find((u) => u.includes("oo1.mjs"));
  const wasm = core.find((u) => u.includes("oo1.wasm"));
  expect(mjs).toMatch(/\/core\/oo1\.mjs\?v=[0-9a-f]{12}$/);
  expect(wasm).toMatch(/\/core\/oo1\.wasm\?v=[0-9a-f]{12}$/);
  expect(new URL(mjs!).searchParams.get("v")).toBe(new URL(wasm!).searchParams.get("v"));
});

test("a desktop gets no touch note", async ({ page }) => {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS", { timeout: 15_000 });
  await expect(page.getByTestId("touch-note")).toHaveCount(0);
});

test.describe("on a touch screen", () => {
  test.use({ hasTouch: true, isMobile: true, viewport: { width: 820, height: 1180 } });
  test.skip(({ browserName }) => browserName !== "chromium", "isMobile is Chromium's");

  test("says the terminal needs a keyboard, once", async ({ page }) => {
    await page.goto("./");
    const note = page.getByTestId("touch-note");
    await expect(note).toContainText("needs a keyboard");
    await note.getByRole("button", { name: "OK" }).click();
    await expect(note).toHaveCount(0);
    await page.reload();
    await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS", { timeout: 15_000 });
    await expect(note).toHaveCount(0);
  });
});

test("served without the trailing slash, it goes to the slash and boots", async ({ page, baseURL }) => {
  // Published (OO1_BASE_URL), the page is a directory and its server answers it without the slash.
  // vite preview serves dist/ at /, which has no slash to leave off, so stand in for a directory
  // URL with routes that answer /emulator with the page itself and /emulator/... with dist/.
  let dir = new URL(baseURL!).pathname.replace(/\/$/, "");
  if (!dir) {
    dir = "/emulator";
    await page.route("**/emulator", async (route) => route.fulfill({ response: await page.request.get("./") }));
    await page.route("**/emulator/**", async (route) => {
      const url = new URL(route.request().url());
      url.pathname = url.pathname.replace("/emulator/", "/");
      route.fulfill({ response: await page.request.get(url.href) });
    });
  }
  await page.goto(`${dir}?from=site`);
  await expect(page).toHaveURL(new URL(`${dir}/?from=site`, baseURL).href);
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS", { timeout: 15_000 });
});
