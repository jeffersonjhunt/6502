// @vitest-environment happy-dom
import { beforeEach, describe, expect, test, vi } from "vitest";
import { biosRow, forgetBios, keepBios, keptBios } from "./bios";

const flush = async () => {
  for (let i = 0; i < 6; ++i) await new Promise((r) => setTimeout(r, 0));
};
const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;

async function pick(el: HTMLElement, name: string, bytes: Uint8Array, size?: number) {
  const input = $(el, "bios-file") as HTMLInputElement;
  const file = new File([new Uint8Array(bytes)], name);
  if (size !== undefined) Object.defineProperty(file, "size", { value: size });
  Object.defineProperty(input, "files", { configurable: true, value: [file] });
  input.dispatchEvent(new Event("change"));
  await flush();
}

describe("the BIOS row", () => {
  beforeEach(() => localStorage.clear());

  test("built in: says which; a build of your own is kept, and the machine restarts on it", async () => {
    let restarts = 0;
    const row = biosRow(() => ++restarts);
    row.connected("0.9");
    expect($(row.element, "bios-meta").textContent).toBe("built-in 0.9 · or load your own build");
    expect($(row.element, "bios-builtin").hidden).toBe(true);
    const rom = Uint8Array.from({ length: 512 * 1024 }, (_, i) => i & 0xff);
    await pick(row.element, "bios-dev.rom", rom);
    expect(restarts).toBe(1);
    expect(keptBios()).toEqual({ name: "bios-dev.rom", bytes: rom });
  });

  test("kept from last time: says so, with its version; Use built-in forgets it and restarts", async () => {
    keepBios("mine.rom", new Uint8Array([1, 2, 3]));
    let restarts = 0;
    const row = biosRow(() => ++restarts);
    expect(row.mine?.name).toBe("mine.rom");
    row.connected("0.10-dev");
    expect($(row.element, "bios-meta").textContent).toBe("yours: mine.rom · 0.10-dev · kept in this browser");
    $(row.element, "bios-builtin").click();
    expect(keptBios()).toBeNull();
    expect(restarts).toBe(1);
  });

  test("a file too big for the flash, or empty, is refused before it is read; nothing restarts", async () => {
    let restarts = 0;
    const row = biosRow(() => ++restarts);
    await pick(row.element, "huge.bin", new Uint8Array(4), 512 * 1024 + 1);
    expect($(row.element, "bios-note").textContent).toBe("huge.bin: 524289 bytes is no BIOS for the flash (up to 512 KB)");
    await pick(row.element, "empty.bin", new Uint8Array(0));
    expect($(row.element, "bios-note").textContent).toBe("empty.bin: 0 bytes is no BIOS for the flash (up to 512 KB)");
    expect(restarts).toBe(0);
    expect(keptBios()).toBeNull();
  });

  test("a browser that won't keep it: says so, and doesn't restart on nothing", async () => {
    let restarts = 0;
    const row = biosRow(() => ++restarts);
    const full = vi.spyOn(localStorage, "setItem").mockImplementation(() => {
      throw new Error("QuotaExceededError");
    });
    try {
      await pick(row.element, "bios.rom", new Uint8Array(16));
    } finally {
      full.mockRestore();
    }
    expect($(row.element, "bios-note").textContent).toBe("This browser would not keep it, so the machine can't restart on it");
    expect(restarts).toBe(0);
    expect(keptBios()).toBeNull();
    forgetBios();
  });
});
