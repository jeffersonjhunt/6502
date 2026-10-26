import { expect, test, type Page } from "@playwright/test";
import { inKeyLoop } from "./bios-labels";

// The Disk II's drive in the browser with a made-up 140 KB disk (no Apple
// files needed): in, decoded on the Devices tab, kept through a reload,
// saved, and out. Booting real disks is iie.spec.ts's, with the owner's.

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
}

const disk = () => Buffer.from(Uint8Array.from({ length: 143_360 }, (_, i) => (i * 7 + (i >> 8)) & 0xff));

test("a disk goes in, shows on the Devices tab, stays through a reload, saves, and comes out", async ({ page }) => {
  await booted(page);
  await expect(page.getByTestId("disk-bar")).toBeVisible();
  await expect(page.getByTestId("disk-eject")).toBeHidden();
  await page.getByTestId("disk-file").setInputFiles({ name: "Random.dsk", mimeType: "application/octet-stream", buffer: disk() });
  await expect(page.getByTestId("disk-insert")).toHaveText("Random.dsk");
  await expect(page.getByTestId("disk-light")).not.toHaveClass(/on/);
  await expect(page.getByTestId("disk-note")).toHaveText("To boot it, the IIe needs the IIe ROM and the Disk II PROMs (Insert IIe ROM…)");
  await page.locator('[data-tab="dev"]').click();
  const dev = page.getByTestId("dev-disk2");
  await expect(dev.locator('[data-reg="Drive 1"]')).toHaveText("Random.dsk · 16 sectors, DOS order");
  await expect(dev.locator('[data-reg="Drive"]')).toHaveText("1 · motor off");

  await page.getByTestId("disk-protect").click();
  await expect(dev.locator('[data-reg="Drive 1"]')).toContainText("write-protected");
  await page.reload();
  await expect(page.getByTestId("disk-insert")).toHaveText("Random.dsk", { timeout: 15_000 });
  await expect(page.getByTestId("disk-protect")).toHaveAttribute("aria-pressed", "true");

  const download = page.waitForEvent("download");
  await page.getByTestId("disk-save").click();
  const file = await download;
  expect(file.suggestedFilename()).toBe("Random.dsk");
  const path = await file.path();
  const { readFileSync } = await import("node:fs");
  expect(readFileSync(path).equals(disk())).toBe(true); // the same 140 KB back

  await page.getByTestId("disk-eject").click();
  await expect(page.getByTestId("disk-insert")).toHaveText("Insert disk…");
  await page.reload();
  await expect(page.getByTestId("crt-text")).toContainText("BACKPLANE BIOS 0.9", { timeout: 15_000 });
  await expect(page.getByTestId("disk-insert")).toHaveText("Insert disk…");
});

test("a file that is no disk: says why", async ({ page }) => {
  await booted(page);
  await page.getByTestId("disk-file").setInputFiles({ name: "notes.txt", mimeType: "text/plain", buffer: Buffer.from("hello") });
  await expect(page.getByTestId("disk-note")).toHaveText(/^notes\.txt: 5 bytes is no disk image/);
  await expect(page.getByTestId("disk-insert")).toHaveText("Insert disk…");
});

test("a browser whose storage never answers still starts: lamps, debugger and keyboard don't wait for the kept disk", async ({ page }) => {
  await page.addInitScript(() => {
    IDBFactory.prototype.open = function () {
      return {} as IDBOpenDBRequest; // neither success nor error, ever
    };
  });
  await booted(page);
  await expect(page.getByTestId("crt")).toBeFocused({ timeout: 3000 });
  await expect
    .poll(() => page.locator('.lamp[data-lamp="A19"]').evaluate((e) => Number((e as HTMLElement).style.getPropertyValue("--b") || 0)), { timeout: 3000 })
    .toBeGreaterThan(0.3);
  await page.locator('[data-tab="cpu"]').click();
  await expect
    .poll(async () => inKeyLoop(parseInt((await page.getByTestId("reg-pc").textContent()) ?? "", 16)), { timeout: 3000 })
    .toBe(true); // the monitor's key loop
});
