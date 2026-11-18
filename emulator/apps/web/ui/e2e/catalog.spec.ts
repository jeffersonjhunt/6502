import { expect, test, type Page } from "@playwright/test";
import { createHash } from "node:crypto";
import { gzipSync } from "node:zlib";
import { openSettings } from "./ui";
import { fakeIIeRom } from "../src/port/fake-iie";

// The disk catalog (ui/catalog.ts) against a catalog made here, from bytes this test makes: served beside the test
// build, on its own origin, by routes, and named to the page by ?catalog= (on this computer only, so not against a
// published page). A card from it goes in the SD slot, a ROM to its own row only, and a catalog that doesn't answer
// says so and leaves the slot as it was.

test.skip(!!process.env.OO1_BASE_URL, "?catalog= is this computer's only");

const DIR = "e2e-catalog/";
const sha = (b: Buffer) => createHash("sha256").update(b).digest("hex");

/** One image in a folder, as the Lambda lists it. */
interface Image {
  file: string;
  bytes: Buffer;
  meta: Record<string, unknown>;
}

/** Serve `folders` (folder → its images; null: the folder doesn't answer; left out: empty) under DIR. @returns the files asked for. */
async function serve(page: Page, folders: Record<string, Image[] | null>) {
  const asked: string[] = [];
  await page.route(`**/${DIR}**`, (route) => {
    const path = new URL(route.request().url()).pathname.split(`/${DIR}`)[1];
    asked.push(path);
    const [folder, file] = path.split("/");
    const images = folders[folder] === undefined ? [] : folders[folder];
    if (images === null) return route.abort("connectionrefused");
    if (file === "index.json") {
      const items = images.map((i) => {
        const gzip = i.bytes[0] === 0x1f && i.bytes[1] === 0x8b;
        return { file: i.file, size: i.bytes.length, modified: "2026-11-18T22:39:58Z", sha256: sha(i.bytes), gzip, draft: false, ...i.meta };
      });
      return route.fulfill({ json: { version: 1, updated: "2026-11-18T22:40:11Z", items, skipped: [] } });
    }
    const image = images.find((i) => i.file === decodeURIComponent(file));
    return image ? route.fulfill({ body: image.bytes, contentType: "application/octet-stream" }) : route.fulfill({ status: 404 });
  });
  return asked;
}

test("a card from the catalog goes in the SD slot, unzipped and checked, as Insert… puts one", async ({ page }) => {
  const card = Buffer.alloc(64 * 512);
  card.write("A CARD FROM THE CATALOG", 512);
  const asked = await serve(page, {
    sd: [{ file: "test-card.img.gz", bytes: gzipSync(card), meta: { title: "Test card", unzipped_size: card.length, description: "Made by the test", draft: true, details: { Made: "here" }, credit: "The test" } }],
  });
  await page.goto(`./?sd=empty&catalog=${DIR}`);
  await expect(page.getByTestId("status")).toHaveText("POWER OFF", { timeout: 15_000 });
  expect(asked).toEqual([]); // nothing fetched before Settings opens
  await openSettings(page);
  const open = page.getByTestId("sd-catalog");
  await expect(open).toBeVisible();
  expect(asked.sort()).toEqual(["disks/index.json", "roms/index.json", "sd/index.json", "sid/index.json"]);
  await expect(page.getByTestId("disk-catalog")).toBeHidden(); // disks/ has nothing
  await open.click();
  const sheet = page.getByTestId("catalog");
  await expect(sheet.getByTestId("catalog-title")).toHaveText("Catalog · SD card");
  const item = sheet.getByTestId("catalog-item");
  await expect(item).toHaveCount(1);
  await expect(item).toContainText("Test card");
  await expect(item.getByTestId("catalog-draft")).toHaveText("draft"); // 127.0.0.1 lists drafts
  await expect(item).toContainText("→ 32 KB");
  await item.click();
  await expect(sheet.getByTestId("catalog-details")).toHaveText("MadehereCreditThe test");
  await sheet.getByTestId("catalog-take").click();
  await expect(sheet).toBeHidden();
  await expect(page.getByTestId("sd-name")).toHaveText("test-card.img");
  await expect(page.getByTestId("sd-meta")).toHaveText("32 KB");
  await expect(page.getByTestId("sd-note")).toHaveText("C809R lists the card; C80CR boots its /BOOT.BIN");
  await expect(open).toBeHidden(); // with Insert…, while a card of the visitor's is in
});

test("a ROM from the catalog goes to its own row only", async ({ page }) => {
  const rom = Buffer.from(fakeIIeRom());
  const prom = Buffer.alloc(256, 0xea);
  await serve(page, {
    roms: [
      { file: "stand-in.rom", bytes: rom, meta: { title: "IIe stand-in", slot: "iie-rom" } },
      { file: "seq.bin", bytes: prom, meta: { title: "A sequencer", slot: "disk2-seq" } },
    ],
  });
  await page.goto(`./?sd=empty&catalog=${DIR}`);
  await openSettings(page);
  await expect(page.getByTestId("apple-catalog")).toBeVisible();
  await expect(page.getByTestId("bios-catalog")).toBeHidden(); // nothing for the BIOS row
  await expect(page.getByTestId("apple-chars-catalog")).toBeHidden();
  await expect(page.getByTestId("apple-seq-catalog")).toBeDisabled(); // with its Load…: the IIe ROM first
  await page.getByTestId("apple-catalog").click();
  const sheet = page.getByTestId("catalog");
  await expect(sheet.getByTestId("catalog-item")).toHaveCount(1);
  await expect(sheet.getByTestId("catalog-item")).toContainText("IIe stand-in");
  await sheet.getByTestId("catalog-item").click();
  await sheet.getByTestId("catalog-take").click();
  await expect(sheet).toBeHidden();
  await expect(page.getByTestId("apple-meta")).toHaveText("16 KB · yours, kept in this browser");
  await expect(page.getByTestId("apple-seq-meta")).toHaveText("none: the drive reads nothing");
  await expect(page.getByTestId("bios-meta")).toHaveText(/^built-in/);
  await expect(page.getByTestId("apple-seq-catalog")).toBeEnabled();
});

test("a catalog that doesn't answer says so, and the slot stays as it was", async ({ page }) => {
  await serve(page, { disks: null });
  await page.goto(`./?sd=empty&catalog=${DIR}`);
  await openSettings(page);
  const open = page.getByTestId("disk-catalog");
  await expect(open).toBeVisible();
  await open.click();
  const sheet = page.getByTestId("catalog");
  await expect(sheet.getByTestId("catalog-title")).toHaveText("Catalog · Disk II, drive 1");
  await expect(sheet.getByTestId("catalog-state")).toContainText("The catalog didn't load");
  await expect(sheet.getByTestId("catalog-state")).toContainText("127.0.0.1:4173 didn't answer. Insert… still takes a disk of your own.");
  await expect(sheet.getByTestId("catalog-retry")).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(sheet).toBeHidden();
  await expect(page.getByTestId("settings")).toBeVisible(); // the sheet closed, not the drawer
  await expect(open).toBeFocused();
  await expect(page.getByTestId("disk-meta")).toHaveText("empty");
});
