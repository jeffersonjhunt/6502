// @vitest-environment happy-dom
import { createHash } from "node:crypto";
import { gzipSync } from "node:zlib";
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { appleBar } from "./apple-bar";
import { catalog, catalogBase, DownloadError, fetchItem, fileSize, forSlot, itemSize, parseIndex, progress, showsDrafts, slotName, type CatalogItem, type Take } from "./catalog";
import { diskBar } from "./disk-bar";
import { memoryDiskStore } from "./disk-store";
import { h } from "./dom";
import { sdBar } from "./sd-bar";
import { sidBar } from "./sid-bar";

const flush = async () => {
  for (let i = 0; i < 10; ++i) await new Promise((r) => setTimeout(r, 0));
};
const $ = (el: ParentNode, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const BASE = "https://assets.example/oo-1/catalogs/";

/** An index entry for `bytes`, as the Lambda writes it. */
function entry(file: string, bytes: Uint8Array, more: Record<string, unknown> = {}) {
  return { file, size: bytes.length, modified: "2026-11-18T22:39:58Z", sha256: sha(bytes), etag: "x", gzip: bytes[0] === 0x1f && bytes[1] === 0x8b, draft: false, title: file, ...more };
}
const index = (items: unknown[]) => ({ version: 1, updated: "2026-11-18T22:40:11Z", items, skipped: [{ file: "oops.img", why: "no oops.img.json" }] });

/** A fetch over `files` (URL → JSON, bytes, or an HTTP status), counting what was asked for. */
function server(files: Record<string, unknown>) {
  const asked: string[] = [];
  const get = (async (input: RequestInfo | URL) => {
    const url = String(input);
    asked.push(url);
    if (!(url in files)) return new Response("no", { status: 404 });
    const f = files[url];
    if (f instanceof Error) throw f;
    if (typeof f === "number") return new Response("", { status: f });
    if (f instanceof Uint8Array) return new Response(new Uint8Array(f));
    return new Response(JSON.stringify(f));
  }) as typeof fetch;
  return { get, asked };
}

describe("the index", () => {
  const card = new Uint8Array(1024).fill(7);
  const gz = new Uint8Array(gzipSync(card));

  test("its entries as the page reads them, the unknown fields ignored", () => {
    const items = parseIndex(index([entry("card.img.gz", gz, { unzipped_size: 1024, title: "A card", description: "One line", details: { Boots: "OODOS 0.11", n: 3, "": "x" }, credit: "Us", protect: true, extra: [1] })]), "sd", false);
    expect(items).toEqual([
      { file: "card.img.gz", size: gz.length, gzip: true, unzipped: 1024, sha256: sha(gz), title: "A card", description: "One line", details: [["Boots", "OODOS 0.11"]], credit: "Us", draft: false, slot: null, protect: true },
    ]);
  });

  test("an entry the page can't use is left out, and the rest stay", () => {
    const good = entry("good.dsk", card);
    const items = parseIndex(
      index([
        good,
        { ...good, title: "" },
        { ...good, title: undefined },
        { ...good, sha256: "abc" },
        { ...good, size: -1 },
        { ...good, size: "1024" },
        { ...good, file: "../up.dsk" },
        { ...good, file: "a/b.dsk" },
        { ...good, file: ".hidden" },
        { ...good, file: "index.json" },
        null,
        "a string",
        [good],
      ]),
      "disks",
      false,
    ) as CatalogItem[];
    expect(items.map((i) => i.file)).toEqual(["good.dsk"]);
  });

  test("not an index: why, not a crash", () => {
    expect(parseIndex(null, "sd", false)).toBe("not a catalog index");
    expect(parseIndex({ version: 2, items: [] }, "sd", false)).toBe("an index of version 2, not 1");
    expect(parseIndex({ items: [] }, "sd", false)).toBe("an index of version null, not 1");
    expect(parseIndex({ version: 1, items: {} }, "sd", false)).toBe("an index with no items");
  });

  test("a ROM lists on its own row only, and one with no row (or an unknown one) not at all", () => {
    const rom = new Uint8Array(16384);
    const items = parseIndex(
      index([entry("iie.rom", rom, { slot: "iie-rom" }), entry("bios.rom", rom, { slot: "bios" }), entry("none.rom", rom), entry("odd.rom", rom, { slot: "rom-b" })]),
      "roms",
      false,
    ) as CatalogItem[];
    expect(items.map((i) => i.file)).toEqual(["iie.rom", "bios.rom"]);
    expect(forSlot(items, "iie-rom").map((i) => i.file)).toEqual(["iie.rom"]);
    expect(forSlot(items, "bios").map((i) => i.file)).toEqual(["bios.rom"]);
    expect(forSlot(items, "disk2-seq")).toEqual([]);
  });

  test("slot and protect mean nothing outside their folders; unzipped_size only for a gzip", () => {
    const [d] = parseIndex(index([entry("x.sid", card, { slot: "bios", protect: true, unzipped_size: 5 })]), "sid", false) as CatalogItem[];
    expect([d.slot, d.protect, d.unzipped]).toEqual([null, false, null]);
  });

  test("drafts list on staging and this computer only", () => {
    expect(["staging.oneoffendeavors.com", "localhost", "127.0.0.1"].every(showsDrafts)).toBe(true);
    expect(["oneoffendeavors.com", "www.oneoffendeavors.com", "staging.oneoffendeavors.com.evil"].some(showsDrafts)).toBe(false);
    const both = index([entry("a.img", card), entry("b.img", card, { draft: true })]);
    expect((parseIndex(both, "sd", false) as CatalogItem[]).map((i) => i.file)).toEqual(["a.img"]);
    expect((parseIndex(both, "sd", true) as CatalogItem[]).map((i) => [i.file, i.draft])).toEqual([["a.img", false], ["b.img", true]]);
  });

  test("sizes: both for a gzip whose unzipped size is given, else the file's", () => {
    expect([fileSize(256), fileSize(16384), fileSize(225402), fileSize(32 * 1024 * 1024), fileSize(1474560)]).toEqual(["256 bytes", "16 KB", "220 KB", "32 MB", "1.4 MB"]);
    const [a, b] = parseIndex(index([entry("a.img.gz", gz, { size: 225402, unzipped_size: 32 * 1024 * 1024 }), entry("b.img.gz", gz, { size: 8192 })]), "sd", false) as CatalogItem[];
    expect([itemSize(a), itemSize(b)]).toEqual(["220 KB → 32 MB", "8 KB"]);
    expect(progress(140 * 1024, 232 * 1024)).toBe("140 of 232 KB");
    expect(slotName(a)).toBe("a.img");
  });
});

describe("the address", () => {
  const prod = { hostname: "oneoffendeavors.com", search: "", href: "https://oneoffendeavors.com/projects/oo-1/emulator/" };
  test("the build's, ending in /; none without one", () => {
    expect(catalogBase("https://assets.oneoffendeavors.com/oo-1/catalogs", prod)).toBe("https://assets.oneoffendeavors.com/oo-1/catalogs/");
    expect(catalogBase(undefined, prod)).toBe(null);
    expect(catalogBase(" ", prod)).toBe(null);
    expect(catalogBase("javascript:alert(1)", prod)).toBe(null);
  });
  test("?catalog= on this computer only", () => {
    const local = { hostname: "127.0.0.1", search: "?catalog=fixture/cat", href: "http://127.0.0.1:4173/?catalog=fixture/cat" };
    expect(catalogBase(undefined, local)).toBe("http://127.0.0.1:4173/fixture/cat/");
    expect(catalogBase("https://a.example/", { ...prod, search: "?catalog=https://elsewhere.example/" })).toBe("https://a.example/");
  });
});

describe("a download", () => {
  const bytes = new Uint8Array(300).map((_, i) => i & 0xff);
  const item = (b: Uint8Array) => (parseIndex(index([entry("f.dsk", b)]), "disks", false) as CatalogItem[])[0];

  test("checked against its checksum, and unzipped by its magic", async () => {
    const gz = new Uint8Array(gzipSync(bytes));
    const { get } = server({ "u/plain": bytes, "u/gz": gz });
    const told: number[] = [];
    expect(await fetchItem("u/plain", item(bytes), { get, told: (got) => told.push(got) })).toEqual(bytes);
    expect(told.at(-1)).toBe(300);
    expect(await fetchItem("u/gz", item(gz), { get })).toEqual(bytes);
  });

  test("a broken one, a missing one and an unzippable one say why", async () => {
    const bad = bytes.slice();
    bad[5] ^= 1;
    const { get } = server({ "u/bad": bad, "u/gone": 404, "u/net": new TypeError("Failed to fetch"), "u/trunc": new Uint8Array([0x1f, 0x8b, 1, 2]) });
    await expect(fetchItem("u/bad", item(bytes), { get })).rejects.toThrow(new DownloadError("the download is broken: its bytes don't match the catalog's checksum"));
    await expect(fetchItem("u/gone", item(bytes), { get })).rejects.toThrow("the download failed: the server answered 404");
    await expect(fetchItem("u/net", item(bytes), { get })).rejects.toThrow("the download failed: Failed to fetch");
    const trunc = new Uint8Array([0x1f, 0x8b, 1, 2]);
    await expect(fetchItem("u/trunc", item(trunc), { get })).rejects.toThrow("a gzip file this page couldn't unzip");
  });
});

/** A catalog over `files`, a Settings panel to sheet over, and one row's button. */
function make(files: Record<string, unknown>, host = "oneoffendeavors.com") {
  const srv = server(files);
  let t = 0;
  const cat = catalog({ base: BASE, host, get: srv.get, now: () => t });
  const body = h("div", { class: "settings-b" }, "the rows");
  const aside = h("aside", {}, h("div", { class: "drawer-h" }, "Settings"), body);
  const drawer = h("div", { class: "drawer" }, aside);
  aside.append(cat.sheet);
  document.body.replaceChildren(drawer);
  const takes: { name: string; size: number; protect: boolean; bytes?: Uint8Array; error?: string }[] = [];
  let answer: string | null = null;
  const take: Take = async (name, size, read, how) => {
    const t1 = { name, size, protect: how.protect } as (typeof takes)[number];
    takes.push(t1);
    try {
      t1.bytes = await read();
    } catch (err) {
      return (t1.error = (err as Error).message);
    }
    return answer;
  };
  const own = h("button", {}, "Insert…");
  const row = h("div", { class: "row" }, own);
  body.append(row);
  return { cat, srv, body, take, takes, own, row, answerWith: (a: string | null) => (answer = a), tick: (ms: number) => (t += ms) };
}

describe("the sheet", () => {
  const card = new Uint8Array(2048).fill(1);
  const gz = new Uint8Array(gzipSync(card));
  const sdIndex = index([entry("demo.img.gz", gz, { title: "Demo card", unzipped_size: 2048, details: { Boots: "OODOS" }, credit: "Us" }), entry("draft.img", card, { title: "Draft card", draft: true, protect: true })]);

  test("nothing is fetched until Settings opens; Catalog… hides until its folder answers, then shows for entries", async () => {
    const m = make({ [`${BASE}sd/index.json`]: sdIndex });
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.row.append(b.button);
    b.follow(m.own);
    await flush();
    expect(m.srv.asked).toEqual([]);
    expect(b.button.hidden).toBe(true);
    m.cat.refresh();
    await flush();
    expect(m.srv.asked).toEqual([`${BASE}sd/index.json`]);
    expect(b.button.hidden).toBe(false);
    m.cat.refresh(); // within the minute: not again
    m.tick(60_000);
    m.cat.refresh();
    await flush();
    expect(m.srv.asked.length).toBe(2);
  });

  test("a row whose folder has nothing for it keeps Catalog… hidden", async () => {
    const m = make({ [`${BASE}roms/index.json`]: index([entry("b.rom", card, { slot: "bios" })]) });
    const iie = m.cat.button("iie-rom", m.take, "apple-catalog");
    const bios = m.cat.button("bios", m.take, "bios-catalog");
    iie.follow(m.own);
    bios.follow(m.own);
    m.cat.refresh();
    await flush();
    expect(m.srv.asked).toEqual([`${BASE}roms/index.json`]); // one fetch for the five ROM rows
    expect([iie.button.hidden, bios.button.hidden]).toEqual([true, false]);
  });

  test("it follows its row's own button: hidden and disabled with it", async () => {
    const m = make({ [`${BASE}sd/index.json`]: sdIndex });
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.cat.refresh();
    await flush();
    m.own.hidden = true;
    b.follow(m.own);
    expect(b.button.hidden).toBe(true);
    m.own.hidden = false;
    (m.own as HTMLButtonElement).disabled = true;
    m.own.title = "Load the Apple IIe ROM first";
    b.follow(m.own);
    expect([b.button.hidden, (b.button as HTMLButtonElement).disabled, b.button.title]).toEqual([false, true, "Load the Apple IIe ROM first"]);
  });

  test("the list over Settings: title, size, file, description, draft tag on staging; details of the one picked", async () => {
    const m = make({ [`${BASE}sd/index.json`]: sdIndex }, "staging.oneoffendeavors.com");
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.row.append(b.button);
    b.follow(m.own);
    m.cat.refresh();
    await flush();
    b.button.click();
    expect(m.body.hidden).toBe(true);
    expect(m.cat.sheet.hidden).toBe(false);
    expect($(m.cat.sheet, "catalog-title").textContent).toBe("Catalog · SD card");
    const items = m.cat.sheet.querySelectorAll<HTMLElement>('[data-testid="catalog-item"]');
    expect(Array.from(items, (i) => i.textContent)).toEqual([`Demo card${gz.length} bytes → 2 KBdemo.img.gz`, "Draft carddraft2 KBdraft.img"]);
    expect(document.activeElement).toBe(items[0]);
    expect($(m.cat.sheet, "catalog-filter").closest("[hidden]")).not.toBe(null); // two entries: no filter
    expect(($(m.cat.sheet, "catalog-take") as HTMLButtonElement).disabled).toBe(true);
    items[0].click();
    expect(items[0].getAttribute("aria-pressed")).toBe("true");
    expect($(m.cat.sheet, "catalog-details").textContent).toBe("BootsOODOSCreditUs");
    expect($(m.cat.sheet, "catalog-take").textContent).toBe("Insert");
    expect(($(m.cat.sheet, "catalog-take") as HTMLButtonElement).disabled).toBe(false);
  });

  test("prod hides drafts", async () => {
    const m = make({ [`${BASE}sd/index.json`]: sdIndex });
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.cat.refresh();
    await flush();
    b.button.click();
    expect(m.cat.sheet.querySelectorAll('[data-testid="catalog-item"]').length).toBe(1);
    expect($(m.cat.sheet, "catalog-draft")).toBe(null);
  });

  test("Insert hands the unzipped, checked bytes to the row's own code under the name without .gz; done, the sheet goes", async () => {
    const m = make({ [`${BASE}sd/index.json`]: sdIndex, [`${BASE}sd/demo.img.gz`]: gz, [`${BASE}sd/draft.img`]: card }, "localhost");
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.row.append(b.button);
    b.follow(m.own);
    m.cat.refresh();
    await flush();
    b.button.click();
    $(m.cat.sheet, "catalog-item").click();
    $(m.cat.sheet, "catalog-take").click();
    await flush();
    expect(m.takes).toEqual([{ name: "demo.img", size: 2048, protect: false, bytes: card }]);
    expect(m.cat.sheet.hidden).toBe(true);
    expect(m.body.hidden).toBe(false);
    expect(document.activeElement).toBe(b.button);
    b.button.click(); // the draft says to protect it
    m.cat.sheet.querySelectorAll<HTMLElement>('[data-testid="catalog-item"]')[1].click();
    $(m.cat.sheet, "catalog-take").click();
    await flush();
    expect(m.takes[1]).toMatchObject({ name: "draft.img", protect: true });
  });

  test("a download that doesn't match its checksum says so, and the sheet stays", async () => {
    const broken = card.slice();
    broken[0] = 9;
    const m = make({ [`${BASE}sd/index.json`]: sdIndex, [`${BASE}sd/demo.img.gz`]: broken });
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.cat.refresh();
    await flush();
    b.button.click();
    $(m.cat.sheet, "catalog-item").click();
    $(m.cat.sheet, "catalog-take").click();
    await flush();
    expect(m.takes[0].error).toBe("the download is broken: its bytes don't match the catalog's checksum");
    expect(m.cat.sheet.hidden).toBe(false);
    expect($(m.cat.sheet, "catalog-note").textContent).toBe("demo.img.gz: the download is broken: its bytes don't match the catalog's checksum. The slot is as it was.");
  });

  test("the row refusing the file: its own words in the sheet, which stays", async () => {
    const m = make({ [`${BASE}sd/index.json`]: sdIndex, [`${BASE}sd/demo.img.gz`]: gz });
    m.answerWith("demo.img: not an SD card image");
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.cat.refresh();
    await flush();
    b.button.click();
    $(m.cat.sheet, "catalog-item").click();
    $(m.cat.sheet, "catalog-take").click();
    await flush();
    expect($(m.cat.sheet, "catalog-note").textContent).toBe("demo.img: not an SD card image");
    expect(m.cat.sheet.hidden).toBe(false);
  });

  test("a catalog that didn't load: Catalog… shows, the sheet says why, Try again fetches again", async () => {
    const files: Record<string, unknown> = { [`${BASE}disks/index.json`]: new TypeError("Failed to fetch") };
    const m = make(files);
    const b = m.cat.button("disk", m.take, "disk-catalog", "Disk II, drive 1");
    m.cat.refresh();
    await flush();
    expect(b.button.hidden).toBe(false);
    b.button.click();
    expect($(m.cat.sheet, "catalog-title").textContent).toBe("Catalog · Disk II, drive 1");
    await flush();
    expect($(m.cat.sheet, "catalog-state").textContent).toBe("The catalog didn't loadassets.example didn't answer. Insert… still takes a disk of your own.Try again");
    expect($(m.cat.sheet, "catalog-take").hidden).toBe(true); // nothing to take: Try again is the way on
    files[`${BASE}disks/index.json`] = index([entry("blank.dsk", card)]);
    $(m.cat.sheet, "catalog-retry").click();
    await flush();
    expect($(m.cat.sheet, "catalog-state").hidden).toBe(true);
    expect(m.cat.sheet.querySelectorAll('[data-testid="catalog-item"]').length).toBe(1);
    expect($(m.cat.sheet, "catalog-take").hidden).toBe(false);
  });

  test("an index it can't read, an error status, and an empty folder", async () => {
    const m = make({ [`${BASE}sd/index.json`]: { version: 9 }, [`${BASE}sid/index.json`]: 503, [`${BASE}disks/index.json`]: index([]) });
    const sd = m.cat.button("sd", m.take, "sd-catalog");
    const sid = m.cat.button("sid", m.take, "sid-catalog");
    const disk = m.cat.button("disk", m.take, "disk-catalog");
    m.cat.refresh();
    await flush();
    expect([sd.button.hidden, sid.button.hidden, disk.button.hidden]).toEqual([false, false, true]);
    sd.button.click();
    expect($(m.cat.sheet, "catalog-state").textContent).toContain("assets.example sent a catalog this page can't read (an index of version 9, not 1).");
    sid.button.click();
    expect($(m.cat.sheet, "catalog-state").textContent).toContain("assets.example answered 503. Play .sid… still takes a tune of your own.");
    disk.button.click(); // hidden, but if it is reached: the empty state
    expect($(m.cat.sheet, "catalog-state").textContent).toContain("Nothing here yet");
  });

  test("more than eight entries: the filter, by title, description or file", async () => {
    const many = Array.from({ length: 9 }, (_, i) => entry(`d${i}.dsk`, card, { title: `Disk ${i}`, description: i === 4 ? "Has the games" : "" }));
    const m = make({ [`${BASE}disks/index.json`]: index(many) });
    const b = m.cat.button("disk", m.take, "disk-catalog");
    m.cat.refresh();
    await flush();
    b.button.click();
    const filter = $(m.cat.sheet, "catalog-filter") as HTMLInputElement;
    expect(filter.closest("[hidden]")).toBe(null);
    filter.value = "GAMES";
    filter.dispatchEvent(new Event("input"));
    expect(Array.from(m.cat.sheet.querySelectorAll('[data-testid="catalog-item"]'), (i) => (i as HTMLElement).dataset.file)).toEqual(["d4.dsk"]);
    filter.value = "zzz";
    filter.dispatchEvent(new Event("input"));
    expect($(m.cat.sheet, "catalog-state").textContent).toContain("No match");
  });

  test("Escape and Cancel close the sheet, not the drawer, focus back on Catalog…", async () => {
    const m = make({ [`${BASE}sd/index.json`]: sdIndex });
    const b = m.cat.button("sd", m.take, "sd-catalog");
    m.row.append(b.button);
    b.follow(m.own);
    let drawerHeard = false;
    m.cat.sheet.closest(".drawer")!.addEventListener("keydown", () => (drawerHeard = true));
    m.cat.refresh();
    await flush();
    b.button.click();
    $(m.cat.sheet, "catalog-item").dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    expect([m.cat.sheet.hidden, m.body.hidden, drawerHeard]).toEqual([true, false, false]);
    expect(document.activeElement).toBe(b.button);
    b.button.click();
    $(m.cat.sheet, "catalog-cancel").click();
    expect(m.cat.sheet.hidden).toBe(true);
    b.button.click();
    m.cat.close(); // Settings hid
    expect([m.cat.sheet.hidden, m.body.hidden]).toEqual([true, false]);
  });
});

describe("each row's Catalog… goes to the row's own code", () => {
  const BYTES = new Uint8Array(1024).fill(3);
  /** A catalog whose `folder` has one entry, `file`, of `bytes` (plus the index's `more`). */
  async function one(folder: string, file: string, bytes: Uint8Array, more: Record<string, unknown> = {}) {
    const { get } = server({ [`${BASE}${folder}/index.json`]: index([entry(file, bytes, more)]), [`${BASE}${folder}/${file}`]: bytes });
    const cat = catalog({ base: BASE, host: "oneoffendeavors.com", get });
    const aside = h("aside", {}, h("div", {}, "rows"), cat.sheet);
    document.body.replaceChildren(aside);
    const pickIt = async (button: HTMLElement) => {
      button.click();
      $(cat.sheet, "catalog-item").click();
      $(cat.sheet, "catalog-take").click();
      await flush();
    };
    return { cat, aside, pickIt };
  }

  test("the SD card: in as Insert… puts it, write-protected if the index says", async () => {
    const c = await one("sd", "blank.img", BYTES, { protect: true });
    const port = new FakePort();
    const bar = sdBar(port, () => {}, { addEventListener: () => {}, confirm: () => true } as unknown as Window, { catalog: c.cat.button });
    c.aside.prepend(bar.element);
    c.cat.refresh();
    await flush();
    expect($(bar.element, "sd-catalog").hidden).toBe(false);
    await c.pickIt($(bar.element, "sd-catalog"));
    expect(port.callsTo("insertSd").map(([b, n]) => [(b as Uint8Array).length, n])).toEqual([[1024, "blank.img"]]);
    expect(port.callsTo("setSdWriteProtect")).toEqual([[true]]);
    expect($(bar.element, "sd-note").textContent).toContain("C809R");
    expect($(bar.element, "sd-catalog").hidden).toBe(true); // with Insert…, while a card of the visitor's is in
  });

  test("a disk in drive 2: as Insert… puts it, and kept", async () => {
    const c = await one("disks", "blank.dsk", BYTES);
    const port = new FakePort();
    const store = memoryDiskStore();
    const bar = diskBar(port, store, () => {}, { catalog: c.cat.button }, 1);
    c.aside.prepend(bar.element);
    c.cat.refresh();
    await flush();
    await c.pickIt($(bar.element, "disk-catalog-2"));
    expect(port.callsTo("insertDisk").map(([d, b, n]) => [d, (b as Uint8Array).length, n])).toEqual([[1, 1024, "blank.dsk"]]);
    expect((await store.get(1))?.name).toBe("blank.dsk");
  });

  test("a ROM of the wrong size is refused with Load…'s own words, the slot as it was", async () => {
    const c = await one("roms", "small.rom", new Uint8Array(100), { slot: "iie-rom" });
    const port = new FakePort();
    const bar = appleBar(port, { started: () => {}, catalog: c.cat.button });
    c.aside.prepend(bar.element);
    c.cat.refresh();
    await flush();
    expect($(bar.element, "apple-catalog").hidden).toBe(false);
    expect($(bar.element, "apple-chars-catalog").hidden).toBe(true); // nothing for that row
    await c.pickIt($(bar.element, "apple-catalog"));
    const why = "small.rom: 100 bytes is no IIe ROM (want 16 KB, two 8 KB halves, a 4 KB character ROM, and the Disk II's two 256-byte PROMs)";
    expect($(bar.element, "apple-note").textContent).toBe(why);
    expect($(c.cat.sheet, "catalog-note").textContent).toBe(why);
    expect(port.callsTo("insertCartridge")).toEqual([]);
  });

  test("a tune: Sound turned on in the click, before the download", async () => {
    const c = await one("sid", "tune.sid", BYTES);
    const port = new FakePort();
    const order: string[] = [];
    const bar = sidBar(port, { soundOn: () => (order.push("on"), true), soundOff: () => order.push("off"), catalog: c.cat.button });
    c.aside.prepend(bar.element);
    c.cat.refresh();
    await flush();
    $(bar.element, "sid-catalog").click();
    $(c.cat.sheet, "catalog-item").click();
    $(c.cat.sheet, "catalog-take").click();
    expect(order).toEqual(["on"]); // synchronously, in the click
    await flush();
    expect(port.callsTo("playSid")).toEqual([[1024, 0]]);
  });
});
