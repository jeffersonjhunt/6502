/**
 * The disk catalog: images the site offers, picked from a sheet over
 * Settings by a row's Catalog… (beside the SD card's and the drives'
 * Insert…, the ROM rows' Load…, Sound's Play…). The images live on the
 * assets site (the build's VITE_CATALOG_BASE), a folder each for sd/,
 * disks/, roms/ and sid/, each with the index.json its Lambda writes:
 * version 1, items[] (the file's name, size, sha256 and gzip flag, and the
 * metadata written beside it), skipped[] (which this page ignores). An entry
 * this page can't read is left out; a ROM's names its row (slot). Drafts
 * list only where the page is tried out: staging, and this computer.
 *
 * Nothing is fetched until Settings opens (refresh()): then each folder a
 * row uses, at most once a minute. A row's Catalog… stays hidden until its
 * folder has answered, then shows if the folder has something for it, or if
 * the folder didn't load (its sheet says why, with Try again). It follows its
 * row's own button: hidden or disabled with it.
 *
 * The slot's action downloads the file, checks it against the index's
 * sha256 (Web Crypto), unzips it if it starts with gzip's magic (as the demo
 * card is), and hands it to the row's own code (Take): the same checks,
 * questions and messages as a file of the visitor's. A failed download
 * leaves the slot as it was, and the sheet says why.
 */

import { h } from "./dom";
import { icon } from "./icons";
import { ungzip } from "./sd-bar";

export type CatalogSlot = "sd" | "disk" | "bios" | "iie-rom" | "iie-chars" | "disk2-boot" | "disk2-seq" | "sid";
export type CatalogFolder = "sd" | "disks" | "roms" | "sid";

export const FOLDER: Record<CatalogSlot, CatalogFolder> = {
  sd: "sd",
  disk: "disks",
  bios: "roms",
  "iie-rom": "roms",
  "iie-chars": "roms",
  "disk2-boot": "roms",
  "disk2-seq": "roms",
  sid: "sid",
};
const ROM_SLOTS: readonly CatalogSlot[] = ["bios", "iie-rom", "iie-chars", "disk2-boot", "disk2-seq"];

/** One image the index lists, as this page reads it. */
export interface CatalogItem {
  file: string;
  /** The file's own size, as served. */
  size: number;
  gzip: boolean;
  /** A gzipped file's size unzipped, if the index says (unzipped_size, from the gzip trailer). */
  unzipped: number | null;
  sha256: string;
  title: string;
  description: string;
  details: [string, string][];
  credit: string;
  draft: boolean;
  /** A ROM's row; null outside roms/. */
  slot: CatalogSlot | null;
  /** A disk or card that goes in write-protected. */
  protect: boolean;
}

/** Where drafts list: staging, and this computer. */
export const DRAFT_HOSTS = ["staging.oneoffendeavors.com", "localhost", "127.0.0.1"];
const LOCAL_HOSTS = ["localhost", "127.0.0.1"];

export const showsDrafts = (host: string): boolean => DRAFT_HOSTS.includes(host);

/**
 * The catalog's address, ending in "/": the build's (VITE_CATALOG_BASE), or on this computer ?catalog=URL (a test's,
 * or one being tried out); relative to the page. null: no catalog, and no Catalog… buttons.
 */
export function catalogBase(built: string | undefined, page: Pick<Location, "hostname" | "search" | "href">): string | null {
  let base = built?.trim() ?? "";
  if (LOCAL_HOSTS.includes(page.hostname)) base = new URLSearchParams(page.search).get("catalog")?.trim() || base;
  if (!base) return null;
  try {
    const url = new URL(base, page.href);
    if (url.protocol !== "https:" && url.protocol !== "http:") return null;
    return url.href.endsWith("/") ? url.href : `${url.href}/`;
  } catch {
    return null;
  }
}

const SHA256 = /^[0-9a-f]{64}$/i;
/** A name in the folder: no path, no hidden file. */
const FILE = /^[^/\\.][^/\\]{0,254}$/;
const text = (v: unknown): string => (typeof v === "string" ? v.trim() : "");
const count = (v: unknown): number | null => (typeof v === "number" && Number.isSafeInteger(v) && v >= 0 ? v : null);

/** One entry of the index, or null if this page can't use it. */
function item(raw: unknown, folder: CatalogFolder): CatalogItem | null {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;
  const r = raw as Record<string, unknown>;
  const file = typeof r.file === "string" ? r.file : "";
  const size = count(r.size);
  const sha256 = text(r.sha256).toLowerCase();
  const title = text(r.title);
  if (!FILE.test(file) || file === "index.json" || size === null || !SHA256.test(sha256) || !title) return null;
  let slot: CatalogSlot | null = null;
  if (folder === "roms") {
    if (!ROM_SLOTS.includes(r.slot as CatalogSlot)) return null; // a ROM says which row it is for
    slot = r.slot as CatalogSlot;
  }
  const details: [string, string][] = [];
  if (r.details && typeof r.details === "object" && !Array.isArray(r.details))
    for (const [k, v] of Object.entries(r.details as Record<string, unknown>)) if (k.trim() && text(v)) details.push([k.trim(), text(v)]);
  return {
    file,
    size,
    gzip: r.gzip === true,
    unzipped: r.gzip === true ? count(r.unzipped_size) : null,
    sha256,
    title,
    description: text(r.description),
    details,
    credit: text(r.credit),
    draft: r.draft === true,
    slot,
    protect: r.protect === true && (folder === "sd" || folder === "disks"),
  };
}

/** A folder's index.json: the entries this page can use (drafts only if `drafts`), or why it can't read it. */
export function parseIndex(json: unknown, folder: CatalogFolder, drafts: boolean): CatalogItem[] | string {
  if (!json || typeof json !== "object") return "not a catalog index";
  const index = json as Record<string, unknown>;
  if (index.version !== 1) return `an index of version ${JSON.stringify(index.version ?? null)}, not 1`;
  if (!Array.isArray(index.items)) return "an index with no items";
  return index.items.map((raw) => item(raw, folder)).filter((i): i is CatalogItem => !!i && (drafts || !i.draft));
}

/** A row's entries among a folder's: a ROM's only on its own row. */
export function forSlot(items: CatalogItem[], slot: CatalogSlot): CatalogItem[] {
  return FOLDER[slot] === "roms" ? items.filter((i) => i.slot === slot) : items;
}

/** "16 KB", "232 KB", "32 MB", "1.4 MB", "256 bytes". */
export function fileSize(n: number): string {
  const MB = 1024 * 1024;
  if (n < 1024) return `${n} bytes`;
  if (n < MB) return `${Math.round(n / 1024)} KB`;
  return n % MB === 0 ? `${n / MB} MB` : `${(n / MB).toFixed(1)} MB`;
}

/** The list's size: both for a gzipped file whose unzipped size the index gives ("220 KB → 32 MB"), else the file's. */
export function itemSize(i: CatalogItem): string {
  return i.unzipped !== null ? `${fileSize(i.size)} → ${fileSize(i.unzipped)}` : fileSize(i.size);
}

/** "140 of 232 KB": a download so far. */
export function progress(got: number, total: number): string {
  const MB = 1024 * 1024;
  if (total < MB) return `${Math.round(got / 1024)} of ${Math.round(total / 1024)} KB`;
  return `${(got / MB).toFixed(1)} of ${(total / MB).toFixed(1)} MB`;
}

/** The name the slot shows: a gzipped file's without its .gz, as it is unzipped. */
export function slotName(i: CatalogItem): string {
  return (i.gzip && i.file.replace(/\.gz$/i, "")) || i.file;
}

const hex = (b: ArrayBuffer) => Array.from(new Uint8Array(b), (v) => v.toString(16).padStart(2, "0")).join("");

/** Why a download didn't come: said in the sheet, the slot as it was. */
export class DownloadError extends Error {}

/**
 * An entry's bytes from `url`: as served, checked against its sha256, then unzipped if they start with gzip's magic.
 * `told`: the bytes so far, as they come.
 */
export async function fetchItem(
  url: string,
  i: CatalogItem,
  how: { get?: typeof fetch; signal?: AbortSignal; told?: (got: number, total: number) => void } = {},
): Promise<Uint8Array> {
  const get = how.get ?? fetch;
  let bytes: Uint8Array;
  try {
    const r = await get(url, { signal: how.signal });
    if (!r.ok) throw new DownloadError(`the download failed: the server answered ${r.status}`);
    const total = Number(r.headers.get("content-length")) || i.size;
    const parts: Uint8Array[] = [];
    let got = 0;
    const reader = r.body?.getReader();
    if (reader) {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        parts.push(value);
        got += value.length;
        how.told?.(got, total);
      }
      bytes = new Uint8Array(got);
      let at = 0;
      for (const p of parts) {
        bytes.set(p, at);
        at += p.length;
      }
    } else bytes = new Uint8Array(await r.arrayBuffer());
  } catch (err) {
    if (err instanceof DownloadError) throw err;
    if (how.signal?.aborted) throw new DownloadError("the download was cancelled");
    throw new DownloadError(`the download failed: ${err instanceof Error ? err.message : String(err)}`);
  }
  if (hex(await crypto.subtle.digest("SHA-256", bytes as Uint8Array<ArrayBuffer>)) !== i.sha256)
    throw new DownloadError("the download is broken: its bytes don't match the catalog's checksum");
  try {
    return await ungzip(bytes);
  } catch {
    throw new DownloadError("a gzip file this page couldn't unzip");
  }
}

/**
 * A row's own code for a file: its checks, questions and messages, as for one the visitor picked. `size`: the file's,
 * for the checks made before it is read; `read`: its bytes. @returns null when it went in; else why not, which the row
 * has said too ("": the visitor said no to a question).
 */
export type Take = (name: string, size: number, read: () => Promise<Uint8Array>, how: { protect: boolean }) => Promise<string | null>;

/** A row's Catalog… button, made by the row. */
export interface CatalogButton {
  button: HTMLButtonElement;
  /** Hidden and disabled with the row's own Insert… or Load… (and its title while disabled): call as the row shows. */
  follow: (own: HTMLElement & { disabled?: boolean }) => void;
}
/** How a row asks for its Catalog…: its slot, its own code, the button's testid, and its name in the sheet's title if not the slot's. */
export type CatalogButtonMaker = (slot: CatalogSlot, take: Take, testid: string, title?: string) => CatalogButton;

interface SlotWords {
  /** The sheet's title, after "Catalog · ". */
  title: string;
  /** What's in it, for the button's name: "an SD card". */
  what: string;
  action: "Insert" | "Load" | "Play";
  /** The way that still works when the catalog doesn't. */
  own: string;
  /** Under the list: where it goes. */
  hint: string;
}

const ROM_HINT = "Kept in this browser, as Load… does; it restarts the machine.";
const SLOTS: Record<CatalogSlot, SlotWords> = {
  sd: { title: "SD card", what: "an SD card", action: "Insert", own: "Insert… still takes a card of your own.", hint: "Goes in this page only, as Insert… does." },
  disk: { title: "Disk II", what: "a disk", action: "Insert", own: "Insert… still takes a disk of your own.", hint: "Kept in this browser, as Insert… does." },
  bios: { title: "BIOS", what: "a BIOS", action: "Load", own: "Load… still takes a BIOS of your own.", hint: "Kept in this browser, as Load… does: the page restarts on it." },
  "iie-rom": { title: "Apple IIe ROM", what: "an Apple IIe ROM", action: "Load", own: "Load… still takes a ROM of your own.", hint: ROM_HINT },
  "iie-chars": { title: "IIe character ROM", what: "a IIe character ROM", action: "Load", own: "Load… still takes a ROM of your own.", hint: ROM_HINT },
  "disk2-boot": { title: "Disk II boot PROM", what: "a Disk II boot PROM", action: "Load", own: "Load… still takes a PROM of your own.", hint: ROM_HINT },
  "disk2-seq": { title: "Disk II sequencer PROM", what: "a Disk II sequencer PROM", action: "Load", own: "Load… still takes a PROM of your own.", hint: ROM_HINT },
  sid: { title: ".sid tune", what: "a .sid tune", action: "Play", own: "Play .sid… still takes a tune of your own.", hint: "Plays on the sound card while this tab is in front." },
};

/** A folder fetched at most this often, as Settings opens (the index is cached for a minute). */
export const FRESH_MS = 60_000;
/** The filter shows for more entries than this. */
export const FILTER_OVER = 8;

interface FolderState {
  /** As last read; null until a fetch succeeds. */
  items: CatalogItem[] | null;
  /** Why the last fetch failed; "" if it didn't. */
  why: string;
  at: number;
  loading: Promise<void> | null;
}

interface Row {
  slot: CatalogSlot;
  take: Take;
  title: string;
  button: HTMLButtonElement;
  hidden: boolean;
}

export interface CatalogOptions {
  base: string;
  /** The page's host: whether drafts list. */
  host: string;
  get?: typeof fetch;
  now?: () => number;
}

export function catalog(opts: CatalogOptions) {
  const get = opts.get ?? ((input: RequestInfo | URL, init?: RequestInit) => fetch(input, init));
  const now = opts.now ?? (() => Date.now());
  const drafts = showsDrafts(opts.host);
  const site = (() => {
    try {
      return new URL(opts.base).host || "The catalog";
    } catch {
      return "The catalog";
    }
  })();
  const folders = new Map<CatalogFolder, FolderState>();
  const rows: Row[] = [];

  // The sheet: over Settings' own content while it's open.
  const heading = h("h2", { id: "catalog-title", tabindex: "-1", "data-testid": "catalog-title" });
  const close = h("button", { class: "tbtn icon close", type: "button", "aria-label": "Close the catalog", title: "Close (Escape)", "data-testid": "catalog-close" }, icon("close"));
  const filter = h("input", { type: "search", class: "cat-filter", placeholder: "Filter", "aria-label": "Filter the catalog", autocomplete: "off", "data-testid": "catalog-filter" });
  const filterRow = h("div", { class: "cat-search", hidden: true }, filter);
  const list = h("ul", { class: "cat-list", "aria-labelledby": "catalog-title", "data-testid": "catalog-list" });
  const state = h("div", { class: "cat-state", hidden: true, "data-testid": "catalog-state" });
  const details = h("dl", { class: "cat-details", hidden: true, "data-testid": "catalog-details" });
  const note = h("p", { class: "row-note cat-note", role: "status", "data-testid": "catalog-note" });
  const hint = h("span", { class: "cat-hint" });
  const cancel = h("button", { class: "tbtn ghost", type: "button", "data-testid": "catalog-cancel" }, "Cancel");
  const action = h("button", { class: "tbtn pri", type: "button", disabled: true, "data-testid": "catalog-take" });
  const foot = h("div", { class: "cat-foot" }, hint, h("span", { class: "cat-acts" }, cancel, action));
  const sheet = h(
    "section",
    { class: "catalog", hidden: true, "aria-labelledby": "catalog-title", "data-testid": "catalog" },
    h("div", { class: "drawer-h" }, heading, close),
    filterRow,
    h("div", { class: "cat-b" }, list, state, details, note),
    foot,
  );

  let open: Row | null = null;
  let selected: CatalogItem | null = null;
  let busy: AbortController | null = null;
  let behind: { el: HTMLElement; hidden: HTMLElement["hidden"] }[] = [];
  let scroller: HTMLElement | null = null;
  let scrolled = 0;

  const folderOf = (r: Row) => folders.get(FOLDER[r.slot]);
  /** Shown: the folder has something for it, or didn't load (the sheet says why). Hidden until it answers. */
  const available = (r: Row) => {
    const f = folderOf(r);
    if (!f) return false;
    if (f.why) return true;
    return !!f.items && forSlot(f.items, r.slot).length > 0;
  };
  const showButtons = () => {
    for (const r of rows) r.button.hidden = r.hidden || !available(r);
  };

  const load = (folder: CatalogFolder): Promise<void> => {
    let f = folders.get(folder);
    if (!f) folders.set(folder, (f = { items: null, why: "", at: 0, loading: null }));
    if (f.loading) return f.loading;
    const st = f;
    st.loading = (async () => {
      let why = "";
      try {
        const r = await get(`${opts.base}${folder}/index.json`);
        if (!r.ok) why = `${site} answered ${r.status}.`;
        else {
          let json: unknown;
          try {
            json = await r.json();
          } catch {
            json = null;
          }
          const parsed = parseIndex(json, folder, drafts);
          if (typeof parsed === "string") why = `${site} sent a catalog this page can't read (${parsed}).`;
          else st.items = parsed;
        }
      } catch {
        why = `${site} didn't answer.`;
      }
      st.why = why;
      st.at = now();
      st.loading = null;
      showButtons();
      if (open && FOLDER[open.slot] === folder) render();
    })();
    return st.loading;
  };

  /** Settings opened: each folder a row uses, unless fetched within the minute. */
  const refresh = () => {
    const used = new Set(rows.map((r) => FOLDER[r.slot]));
    for (const folder of used) {
      const f = folders.get(folder);
      if (!f || f.why || now() - f.at >= FRESH_MS) void load(folder);
    }
  };

  const say = (text: string) => (note.textContent = text);
  const entries = (): CatalogItem[] => {
    const f = open && folderOf(open);
    return f?.items && open ? forSlot(f.items, open.slot) : [];
  };

  const showState = (title: string, body: string, retry: boolean, error = false) => {
    state.replaceChildren(h("b", {}, title), body);
    if (retry) {
      const again = h("button", { class: "tbtn", type: "button", "data-testid": "catalog-retry" }, "Try again");
      again.addEventListener("click", () => {
        if (!open) return;
        void load(FOLDER[open.slot]);
        render();
        heading.focus();
      });
      state.append(h("span", { class: "cat-again" }, again));
    }
    state.classList.toggle("err", error);
    state.hidden = false;
  };

  const showDetails = () => {
    const rowsOf: [string, string][] = selected ? [...selected.details, ...(selected.credit ? [["Credit", selected.credit] as [string, string]] : [])] : [];
    details.replaceChildren(...rowsOf.flatMap(([k, v]) => [h("dt", {}, k), h("dd", {}, v)]));
    details.hidden = !rowsOf.length;
  };

  const showAction = () => {
    if (!open) return;
    action.textContent = SLOTS[open.slot].action;
    action.disabled = !selected || !!busy;
    action.setAttribute("aria-label", selected ? `${SLOTS[open.slot].action} ${selected.title}` : SLOTS[open.slot].action);
  };

  /** The list, or the state the folder is in. */
  const render = () => {
    if (!open) return;
    const words = SLOTS[open.slot];
    const f = folderOf(open);
    const all = entries();
    state.hidden = true;
    list.replaceChildren();
    if (!f || (f.loading && !f.items && !f.why)) showState("Loading the catalog…", "", false);
    else if (f.why && !f.items) showState("The catalog didn't load", `${f.why} ${words.own}`, true, true);
    else if (!all.length) showState("Nothing here yet", `The catalog has nothing for this row. ${words.own}`, false);
    filterRow.hidden = all.length <= FILTER_OVER;
    action.hidden = hint.hidden = !all.length; // nothing to take: the state alone, as Try again is its way on
    const q = filterRow.hidden ? "" : filter.value.trim().toLowerCase();
    const shown = q ? all.filter((i) => `${i.title}\n${i.description}\n${i.file}`.toLowerCase().includes(q)) : all;
    if (selected && !all.some((i) => i.file === selected!.file)) selected = null;
    for (const i of shown) {
      const b = h(
        "button",
        { class: "cat-item", type: "button", "aria-pressed": String(selected?.file === i.file), "data-file": i.file, "data-testid": "catalog-item" },
        h("span", { class: "cat-n" }, i.title, ...(i.draft ? [h("span", { class: "cat-draft", "data-testid": "catalog-draft" }, "draft")] : [])),
        h("span", { class: "cat-s" }, itemSize(i)),
        h("span", { class: "cat-f" }, i.file),
        ...(i.description ? [h("span", { class: "cat-d" }, i.description)] : []),
        h("span", { class: "cat-p", "data-testid": "catalog-progress" }),
      );
      b.addEventListener("click", () => pick(i));
      b.addEventListener("dblclick", () => {
        pick(i);
        void take();
      });
      list.append(h("li", {}, b));
    }
    if (all.length && !shown.length) showState("No match", "Nothing in the catalog matches the filter.", false);
    list.hidden = !shown.length;
    showDetails();
    showAction();
  };

  const itemButton = (i: CatalogItem | null) => (i ? Array.from(list.querySelectorAll<HTMLButtonElement>(".cat-item")).find((b) => b.dataset.file === i.file) : undefined);

  const pick = (i: CatalogItem) => {
    if (busy) return;
    selected = i;
    for (const b of list.querySelectorAll<HTMLButtonElement>(".cat-item")) b.setAttribute("aria-pressed", String(b.dataset.file === i.file));
    say("");
    showDetails();
    showAction();
  };

  /** The selected entry, downloaded and handed to the row's own code. */
  const take = async () => {
    if (!open || !selected || busy) return;
    const row = open;
    const i = selected;
    const ctl = new AbortController();
    busy = ctl;
    showAction();
    say("");
    const line = itemButton(i)?.querySelector<HTMLElement>(".cat-p");
    if (line) line.textContent = "Downloading…";
    let failed = "";
    const read = async () => {
      try {
        return await fetchItem(`${opts.base}${FOLDER[row.slot]}/${encodeURIComponent(i.file)}`, i, {
          get,
          signal: ctl.signal,
          told: (got, total) => line && (line.textContent = `Downloading… ${progress(got, total)}`),
        });
      } catch (err) {
        failed = err instanceof Error ? err.message : String(err);
        throw err;
      }
    };
    // Called here, in the click: a row that starts sound (Play) does so in the gesture, before the download.
    const why = await row.take(slotName(i), i.unzipped ?? i.size, read, { protect: i.protect });
    if (busy === ctl) busy = null;
    if (line) line.textContent = "";
    if (open !== row || ctl.signal.aborted) return; // closed meanwhile
    if (why === null) return hide(true);
    showAction();
    if (failed) say(`${i.file}: ${failed}. The slot is as it was.`);
    else say(why);
  };

  const show = (row: Row) => {
    const parent = sheet.parentElement;
    if (!parent) return;
    if (!open) {
      behind = Array.from(parent.children)
        .filter((c): c is HTMLElement => c !== sheet && c instanceof HTMLElement)
        .map((el) => ({ el, hidden: el.hidden }));
      scroller = parent.closest<HTMLElement>(".drawer");
      scrolled = scroller?.scrollTop ?? 0;
    }
    open = row;
    selected = null;
    filter.value = "";
    say("");
    heading.textContent = `Catalog · ${row.title}`;
    hint.textContent = SLOTS[row.slot].hint;
    for (const b of behind) b.el.hidden = true;
    sheet.hidden = false;
    if (scroller) scroller.scrollTop = 0;
    const f = folderOf(row);
    if (!f || f.why || (!f.loading && now() - f.at >= FRESH_MS)) void load(FOLDER[row.slot]);
    render();
    (list.querySelector<HTMLElement>(".cat-item") ?? heading).focus({ preventScroll: true });
  };

  /** Close the sheet, any download with it; `focus`: back to the row (its Catalog…, or what took its place). */
  function hide(focus = true): void {
    if (!open) return;
    const row = open;
    busy?.abort();
    busy = null;
    open = null;
    selected = null;
    sheet.hidden = true;
    for (const b of behind) b.el.hidden = b.hidden;
    behind = [];
    if (scroller) scroller.scrollTop = scrolled;
    if (!focus) return;
    const back =
      !row.button.hidden && !row.button.disabled
        ? row.button
        : Array.from(row.button.closest(".row")?.querySelectorAll<HTMLButtonElement>("button") ?? []).find((b) => !b.hidden && !b.disabled);
    back?.focus();
  }

  close.addEventListener("click", () => hide());
  cancel.addEventListener("click", () => hide());
  action.addEventListener("click", () => void take());
  filter.addEventListener("input", () => render());
  sheet.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      e.preventDefault();
      e.stopPropagation(); // the sheet, not the drawer
      hide();
      return;
    }
    if (e.key !== "ArrowDown" && e.key !== "ArrowUp") return;
    const items = Array.from(list.querySelectorAll<HTMLButtonElement>(".cat-item"));
    const at = items.indexOf(document.activeElement as HTMLButtonElement);
    if (at < 0) return;
    e.preventDefault();
    items[Math.max(0, Math.min(items.length - 1, at + (e.key === "ArrowDown" ? 1 : -1)))].focus();
  });

  const button: CatalogButtonMaker = (slot, takeIt, testid, title = SLOTS[slot].title) => {
    const b = h(
      "button",
      { class: "tbtn", type: "button", hidden: true, "aria-label": `Choose ${SLOTS[slot].what} from the catalog`, title: `The site's catalog: ${SLOTS[slot].what} to ${SLOTS[slot].action.toLowerCase()}`, "data-testid": testid },
      "Catalog…",
    );
    const row: Row = { slot, take: takeIt, title, button: b, hidden: false };
    rows.push(row);
    const own = b.title;
    b.addEventListener("click", () => show(row));
    return {
      button: b,
      follow: (of) => {
        row.hidden = !!of.hidden;
        b.disabled = !!of.disabled;
        b.title = of.disabled ? of.title : own;
        b.hidden = row.hidden || !available(row);
      },
    };
  };

  return {
    /** Goes in Settings' panel, after its content. */
    sheet,
    button,
    refresh,
    /** Settings hid: the sheet goes, focus left where the drawer puts it. */
    close: () => hide(false),
    isOpen: () => open !== null,
  };
}
