import { expect, test, type Page } from "@playwright/test";
import { openDebugger, openSettings } from "./ui";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { bios, hex4, loadBiosLabels } from "./bios-labels";

// The SD card's slot in the browser: test/host/sd's FAT12 image (made by
// mkfs.fat and mtools) listed by C809R and booted by C80CR, and a raw card
// written by a program typed at the monitor, saved with the write.

const GREETING = "BACKPLANE BIOS 0.9";

async function booted(page: Page) {
  await page.goto("./");
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await loadBiosLabels(page);
}

/** A test image from its sparse dump ("LBA hex" for each sector that isn't zeros). */
function image(name: string): Buffer {
  const text = readFileSync(fileURLToPath(new URL(`../../../../test/host/sd/${name}`, import.meta.url)), "utf8").split("\n");
  const sectors = Number(text[1].split(" ")[1]);
  const bytes = Buffer.alloc(sectors * 512);
  for (const line of text.slice(2)) {
    const [lba, hex] = line.split(" ");
    if (hex) Buffer.from(hex, "hex").copy(bytes, Number(lba) * 512);
  }
  return bytes;
}

async function command(page: Page, line: string) {
  await page.keyboard.type(line);
  await page.keyboard.press("Enter");
}

test("a FAT card goes in, C809R lists it and C80CR boots its BOOT.BIN", async ({ page }) => {
  await booted(page);
  await openSettings(page);
  await expect(page.getByTestId("sd-bar")).toBeVisible();
  await expect(page.getByTestId("sd-eject")).toBeHidden();
  await page.getByTestId("sd-file").setInputFiles({ name: "fat12.img", mimeType: "application/octet-stream", buffer: image("fat12.sparse") });
  await expect(page.getByTestId("sd-name")).toHaveText("fat12.img");
  await expect(page.getByTestId("sd-meta")).toHaveText("1440 KB");
  await expect(page.getByTestId("pill-sd")).toHaveText("SD 1440 KB");
  await expect(page.getByTestId("sd-note")).toHaveText("C809R lists the card; C80CR boots its /BOOT.BIN");
  await page.getByRole("application", { name: "Terminal: click and type" }).click(); // the keys back to the terminal
  await command(page, "C809R");
  const crt = page.getByTestId("crt-text");
  await expect(crt).toContainText("CARD fat12.img 00000B40 SECTORS");
  await expect(crt).toContainText("DISKS/");
  await expect(crt).toContainText("BOOT.BIN 00000200");
  await command(page, "C80CR");
  await expect(crt).toContainText("BOOTED FROM SD");
  await expect(page.getByTestId("sd-save")).not.toHaveClass(/on/); // read, not written
  await openDebugger(page);
  await page.locator('[data-tab="dev"]').click();
  const dev = page.getByTestId("dev-sd");
  await expect(dev.locator('[data-reg="Card"]')).toHaveText("fat12.img · 2880 sectors (1440 KB)");
  await expect(dev.locator('[data-reg="STATUS"]')).toHaveText("$60  ready");
  await expect(dev.locator('[data-reg="Transfer"]')).toHaveText("unit 0 · LBA $00000001 · COUNT 65536"); // BOOT.BIN's one sector read; the BIOS put UNIT back
  await page.screenshot({ path: `test-results/sd-${test.info().project.name}.png` });
});

test("the machine's writes are noted, and Save card downloads them; the page doesn't keep the card", async ({ page }) => {
  await booted(page);
  const raw = Buffer.alloc(64 * 512);
  await page.getByTestId("sd-file").setInputFiles({ name: "raw.img", mimeType: "application/octet-stream", buffer: raw });
  await expect(page.getByTestId("sd-name")).toHaveText("raw.img");
  await expect(page.getByTestId("sd-meta")).toHaveText("32 KB");
  await page.getByRole("application", { name: "Terminal: click and type" }).click(); // the keys back to the terminal
  // WRITE sector 0, $AA throughout, by a CPU loop, then JMP back to the monitor (WOZMON).
  const woz = hex4(bios("WOZMON"));
  await command(page, "1000: A9 01 8D 8A C3 9C 8B C3 9C 84 C3 9C 86 C3 A9 30 8D 83 C3");
  await command(page, `1013: A0 00 A9 AA 8D 8C C3 C8 D0 FA 8D 8C C3 C8 D0 FA 4C ${woz.slice(2)} ${woz.slice(0, 2)}`);
  await command(page, "1000R");
  await expect(page.getByTestId("crt-text")).toHaveText(/\\$/); // back at the monitor's prompt
  await expect(page.getByTestId("sd-note")).toHaveText("Written to: Save to keep it, as the page forgets it");
  await expect(page.getByTestId("sd-save")).toHaveClass(/on/);

  await openSettings(page);
  const download = page.waitForEvent("download");
  await page.getByTestId("sd-save").click();
  const file = await download;
  expect(file.suggestedFilename()).toBe("raw.img");
  const saved = readFileSync(await file.path());
  expect(saved.length).toBe(64 * 512);
  expect(saved.subarray(0, 512).every((b) => b === 0xaa)).toBe(true);
  expect(saved.subarray(512).every((b) => b === 0)).toBe(true);
  await expect(page.getByTestId("sd-save")).not.toHaveClass(/on/);

  await page.reload(); // saved: nothing to ask about, and the card is gone with the page
  await expect(page.getByTestId("crt-text")).toContainText(GREETING, { timeout: 15_000 });
  await expect(page.getByTestId("sd-meta")).toHaveText("empty");
});

test("a file that isn't a card is refused, and says why", async ({ page }) => {
  await booted(page);
  await page.getByTestId("sd-file").setInputFiles({ name: "notes.txt", mimeType: "text/plain", buffer: Buffer.from("hello") });
  await expect(page.getByTestId("sd-note")).toHaveText("notes.txt: not an SD card image (less than one 512-byte sector)");
  await expect(page.getByTestId("sd-eject")).toBeHidden();
});
