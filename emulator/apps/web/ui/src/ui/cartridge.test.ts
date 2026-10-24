// @vitest-environment happy-dom
import { beforeEach, describe, expect, test, vi } from "vitest";
import { fakeIIeRom } from "../port/fake-iie";
import { forgetCartridge, keepCartridge, keptCartridge, sortRoms } from "./cartridge";

const K = 1024;
const file = (name: string, bytes: Uint8Array) => ({ name, bytes });

/** The real halves' tell-tales: EF has the ID byte and a reset vector into ROM; CD has neither. */
function halves(): { cd: Uint8Array; ef: Uint8Array } {
  const rom = fakeIIeRom();
  rom[0] = 0x4c; // something to know CD by
  return { cd: rom.slice(0, 8 * K), ef: rom.slice(8 * K) };
}

describe("sortRoms", () => {
  test("one 16 KB ROM, and a 4 KB character ROM", () => {
    const chars = new Uint8Array(4 * K).fill(7);
    const c = sortRoms([file("video.bin", chars), file("iie.bin", fakeIIeRom())]);
    expect(typeof c).toBe("object");
    if (typeof c === "string") return;
    expect(c.rom).toEqual(fakeIIeRom());
    expect(c.chars).toBe(chars);
  });

  test("the CD and EF halves, in either order, go together CD first", () => {
    const { cd, ef } = halves();
    for (const files of [[file("ef", ef), file("cd", cd)], [file("cd", cd), file("ef", ef)]]) {
      const c = sortRoms(files);
      if (typeof c === "string") throw new Error(c);
      expect(c.rom[0]).toBe(0x4c);
      expect(c.rom[0xfbb3 - 0xc000]).toBe(0x06);
      expect(c.chars).toBeNull();
    }
  });

  test("an 8 KB character ROM beside the halves, or beside a whole ROM", () => {
    const { cd, ef } = halves();
    const chars8 = new Uint8Array(8 * K); // no ID byte: not EF
    const c = sortRoms([file("cd", cd), file("chars", chars8), file("ef", ef)]);
    if (typeof c === "string") throw new Error(c);
    expect(c.rom[0]).toBe(0x4c);
    expect(c.chars).toBe(chars8);
    const tricky = new Uint8Array(8 * K);
    tricky[0x1bb3] = 0x06; // a character ROM with the ID byte's value there, but no reset vector: not EF
    const e = sortRoms([file("cd", cd), file("tricky", tricky), file("ef", ef)]);
    if (typeof e === "string") throw new Error(e);
    expect(e.rom[0xfffd - 0xc000]).toBe(0xfa);
    expect(e.chars).toBe(tricky);
    const d = sortRoms([file("iie", fakeIIeRom()), file("chars", chars8)]);
    if (typeof d === "string") throw new Error(d);
    expect(d.chars).toBe(chars8);
  });

  test("says what is wrong", () => {
    const { cd, ef } = halves();
    expect(sortRoms([file("x.zip", new Uint8Array(1234))])).toMatch(/^x\.zip: 1234 bytes is no IIe ROM/);
    expect(sortRoms([file("ef", ef)])).toMatch(/^ef: the \$E000-\$FFFF half/);
    expect(sortRoms([file("cd", cd)])).toMatch(/^No IIe ROM among these/);
    expect(sortRoms([file("a", fakeIIeRom()), file("b", fakeIIeRom())])).toMatch(/two 16 KB ROMs/);
    const plus = fakeIIeRom();
    plus[0xfbb3 - 0xc000] = 0xea; // an Apple II Plus
    expect(sortRoms([file("ii+", plus)])).toBe("Not an Apple IIe ROM: $FBB3 is $EA, a IIe's is $06");
  });
});

describe("kept in this browser", () => {
  beforeEach(() => localStorage.clear());

  test("a browser that will not keep it says so", () => {
    const refuse = vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
      throw new Error("QuotaExceededError");
    });
    try {
      expect(keepCartridge({ rom: fakeIIeRom(), chars: null })).toBe(false);
      expect(keptCartridge()).toBeNull();
    } finally {
      refuse.mockRestore();
    }
  });

  test("kept, back next time, forgotten", () => {
    expect(keptCartridge()).toBeNull();
    const chars = Uint8Array.from({ length: 4 * K }, (_, i) => i & 0xff);
    expect(keepCartridge({ rom: fakeIIeRom(), chars })).toBe(true);
    const back = keptCartridge();
    expect(back?.rom).toEqual(fakeIIeRom());
    expect(back?.chars).toEqual(chars);
    expect(keepCartridge({ rom: fakeIIeRom(), chars: null })).toBe(true);
    expect(keptCartridge()?.chars).toBeNull(); // the old character ROM went with the old cartridge
    forgetCartridge();
    expect(keptCartridge()).toBeNull();
    expect(Object.keys(localStorage)).toEqual([]);
  });

  test("something else under its name is not a cartridge", () => {
    localStorage.setItem("oo1.apple-rom", "not base64!");
    expect(keptCartridge()).toBeNull();
    localStorage.setItem("oo1.apple-rom", btoa("short"));
    expect(keptCartridge()).toBeNull();
  });
});
