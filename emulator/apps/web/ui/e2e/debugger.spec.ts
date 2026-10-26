import { expect, test, type Page } from "@playwright/test";
import { bios, hex4, inKeyLoop } from "./bios-labels";

async function booted(page: Page) {
  await page.goto("./");
  await page.evaluate(() => localStorage.clear());
  await page.reload();
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
}

const pc = (page: Page) => page.getByTestId("reg-pc").textContent();

async function addBreakpoint(page: Page, text: string) {
  await page.locator('[data-tab="bps"]').click();
  await page.getByTestId("bp-input").fill(text);
  await page.getByTestId("bp-add").click();
  await expect(page.getByTestId("bp-row").last()).toContainText(text.toUpperCase().replace(/^\$/, ""));
}

test("CPU tab: live registers; halted, Step moves PC one instruction", async ({ page }) => {
  await booted(page);
  await page.locator('[data-tab="cpu"]').click();
  const pcNow = async () => parseInt((await page.getByTestId("reg-pc").textContent()) ?? "", 16);
  await expect.poll(async () => inKeyLoop(await pcNow())).toBe(true); // the monitor's key loop (GETKEY)
  await page.getByTestId("tb-halt").click();
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  const at = await pc(page);
  await page.waitForTimeout(200);
  expect(await pc(page)).toBe(at); // halted: still
  await page.getByTestId("tb-step").click();
  await expect.poll(() => pc(page)).not.toBe(at);
  await page.screenshot({ path: `test-results/debugger-cpu-${test.info().project.name}.png` });
});

test("a fetch breakpoint on a label halts before ECHO, shows the hit, and Run goes on", async ({ page }) => {
  await booted(page);
  await addBreakpoint(page, "ECHO");
  const echo = hex4(bios("ECHO"));
  await expect(page.getByTestId("bp-row")).toContainText(`ECHO $${echo} → F${echo}`);
  await expect(page.getByTestId("bp-row")).toHaveAttribute("data-armed", "true");
  await page.getByTestId("crt").click();
  await page.keyboard.type("A");
  await expect(page.getByTestId("status")).toHaveText(/HALTED/);
  await expect(page.getByTestId("bp-hit-text")).toHaveText(`hit: F${echo}  data 20  fetch`); // JSR VPUTC
  await expect(page.getByTestId("crt-text")).not.toContainText("\\\nA"); // stopped before the echo
  await page.screenshot({ path: `test-results/debugger-bp-${test.info().project.name}.png` });
  await page.locator('[data-tab="cpu"]').click();
  await expect(page.getByTestId("reg-pc")).toHaveText(echo);
  await page.locator('[data-tab="bps"]').click();
  await page.getByRole("radio", { name: "None armed" }).check(); // disarm, or it stops at the next echo
  await page.getByTestId("tb-run").click();
  await expect(page.getByTestId("status")).toContainText("RUNNING");
  await expect(page.getByTestId("crt-text")).toContainText("\\\nA");
  await expect(page.getByTestId("bp-hit")).toBeVisible(); // until cleared
  await page.getByTestId("bp-clear").click();
  await expect(page.getByTestId("bp-hit")).toBeHidden();
});

test("a write breakpoint can pull /NMI instead: the monitor restarts, nothing halts", async ({ page }) => {
  await booted(page);
  await addBreakpoint(page, "00300");
  const row = page.getByTestId("bp-row");
  await row.locator('[data-flag="write"]').check(); // before unticking fetch: a comparator must match something
  await row.locator('[data-flag="fetch"]').uncheck();
  await row.locator('[data-flag="nmi"]').check();
  await expect(row).toContainText("armed → /NMI");
  await page.getByTestId("crt").click();
  await page.keyboard.type("300: 7");
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("bp-hit-text")).toHaveText("hit: 00300  data 07  write");
  // Woz shows the old byte ("0300: 00"), stores, and the NMI lands in WOZMON, whose "\" follows at once.
  await expect(page.getByTestId("crt-text")).toHaveText(/300: 7\n\n0300: 00\\$/);
  await expect(page.getByTestId("status")).toContainText("RUNNING");
});

test("Bus trace: the monitor's loop, fetches in GETKEY's (bank F), the 6551 and the USB card in the I/O hole", async ({ page }) => {
  await booted(page);
  await page.locator('[data-tab="trace"]').click();
  const rows = page.getByTestId("trace-rows").locator("tr");
  await expect.poll(() => rows.count()).toBeGreaterThan(30);
  const fetch = page.getByTestId("trace-rows").locator("tr.f").first();
  const physical = parseInt((await fetch.locator("td").nth(1).textContent()) ?? "", 16);
  expect(physical >> 16).toBe(0xf); // the RAM copy, in bank F
  expect(inKeyLoop(physical & 0xffff)).toBe(true);
  const io = page.getByTestId("trace-rows").locator("tr.io");
  await expect(io.filter({ hasText: "0C301" }).first().locator("td").nth(5)).toHaveText("I/O m6"); // ACIA status
  await expect(io.filter({ hasText: "0C5FF" }).first().locator("td").nth(5)).toHaveText("I/O mB"); // USB card's ID
  await page.screenshot({ path: `test-results/debugger-trace-${test.info().project.name}.png` });
});

test("the pane collapses, the CRT grows, and it is remembered", async ({ page }) => {
  await page.setViewportSize({ width: 1000, height: 1300 }); // tall: the CRT's size is set by the width beside the pane
  await booted(page);
  const width = () => page.getByTestId("crt").evaluate((e) => e.getBoundingClientRect().width);
  const open = await width();
  await page.getByTestId("tb-debugger").click();
  await expect(page.getByTestId("dbg-reopen")).toBeVisible();
  await expect.poll(width).toBeGreaterThan(open);
  await page.reload();
  await expect(page.getByTestId("dbg-reopen")).toBeVisible();
  await page.getByTestId("dbg-reopen").click();
  await expect(page.getByTestId("debugger")).toBeVisible();
});
