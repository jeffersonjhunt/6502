// @vitest-environment happy-dom
import { beforeEach, describe, expect, test, vi } from "vitest";
import { fakeIIeRom } from "../port/fake-iie";
import { forgetCartridge, keepCartridge, keptCartridge, sortRoms, takePart, withoutPart } from "./cartridge";

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

describe("the Disk II's PROMs", () => {
  const boot = () => {
    const b = new Uint8Array(256);
    b.set([0xa2, 0x20, 0xa0, 0x00, 0xa2, 0x03, 0x86, 0x3c], 0); // a disk controller's signature at 1, 3, 5, 7
    return b;
  };
  const sequencer = () => new Uint8Array(256).fill(0x18);

  test("a sequencer starting as a boot ROM does is still a sequencer: the signature decides", () => {
    const seq = sequencer();
    seq[0] = 0xa2;
    const c = sortRoms([file("p5", boot()), file("p6", seq), file("iie", fakeIIeRom())]);
    if (typeof c === "string") throw new Error(c);
    expect(c.diskBoot).toEqual(boot());
    expect(c.diskSequencer).toEqual(seq);
  });

  test("told apart by the boot ROM's signature, beside the IIe ROM", () => {
    const c = sortRoms([file("p6", sequencer()), file("iie", fakeIIeRom()), file("p5", boot())]);
    if (typeof c === "string") throw new Error(c);
    expect(c.diskBoot).toEqual(boot());
    expect(c.diskSequencer).toEqual(sequencer());
  });

  test("picked alone, they join the cartridge already in; alone with none, they are no cartridge", () => {
    const current = { rom: fakeIIeRom(), chars: new Uint8Array(4 * K), diskBoot: null, diskSequencer: null };
    const c = sortRoms([file("p5", boot()), file("p6", sequencer())], current);
    if (typeof c === "string") throw new Error(c);
    expect(c.rom).toBe(current.rom);
    expect(c.chars).toBe(current.chars);
    expect(c.diskBoot).toEqual(boot());
    expect(sortRoms([file("p5", boot())])).toMatch(/^No IIe ROM among these/);
    expect(sortRoms([file("a", boot()), file("b", boot())])).toMatch(/two Disk II boot ROMs/);
  });

  test("kept with the ROM", () => {
    localStorage.clear();
    keepCartridge({ rom: fakeIIeRom(), chars: null, diskBoot: boot(), diskSequencer: sequencer() });
    const back = keptCartridge();
    expect(back?.diskBoot).toEqual(boot());
    expect(back?.diskSequencer).toEqual(sequencer());
    forgetCartridge();
    expect(Object.keys(localStorage)).toEqual([]);
  });
});

describe("kept in this browser", () => {
  beforeEach(() => localStorage.clear());

  test("a browser that will not keep it says so", () => {
    const refuse = vi.spyOn(localStorage, "setItem").mockImplementation(() => {
      throw new Error("QuotaExceededError");
    });
    try {
      expect(keepCartridge({ rom: fakeIIeRom(), chars: null, diskBoot: null, diskSequencer: null })).toBe(false);
      expect(keptCartridge()).toBeNull();
    } finally {
      refuse.mockRestore();
    }
  });

  test("kept, back next time, forgotten", () => {
    expect(keptCartridge()).toBeNull();
    const chars = Uint8Array.from({ length: 4 * K }, (_, i) => i & 0xff);
    expect(keepCartridge({ rom: fakeIIeRom(), chars, diskBoot: null, diskSequencer: null })).toBe(true);
    const back = keptCartridge();
    expect(back?.rom).toEqual(fakeIIeRom());
    expect(back?.chars).toEqual(chars);
    expect(keepCartridge({ rom: fakeIIeRom(), chars: null, diskBoot: null, diskSequencer: null })).toBe(true);
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

describe("takePart: a row of Settings at a time", () => {
  const boot = () => {
    const b = new Uint8Array(256);
    b.set([0xa2, 0x20, 0xa0, 0x00, 0xa2, 0x03, 0x86, 0x3c]); // the controller's signature
    return b;
  };
  const seq = () => new Uint8Array(256).fill(0x18);

  test("the IIe ROM: whole or in halves, keeping the parts already in", () => {
    const first = takePart("rom", [file("iie.rom", fakeIIeRom())], null);
    if (typeof first === "string") throw new Error(first);
    expect(first.chars).toBeNull();
    const withChars = takePart("chars", [file("video.bin", new Uint8Array(4 * K))], first);
    if (typeof withChars === "string") throw new Error(withChars);
    const { cd, ef } = halves();
    const again = takePart("rom", [file("ef", ef), file("cd", cd)], withChars);
    if (typeof again === "string") throw new Error(again);
    expect(again.rom[0]).toBe(0x4c);
    expect(again.chars).toBe(withChars.chars); // still in
  });

  test("the others need the IIe ROM, one file each, of their own kind", () => {
    expect(takePart("chars", [file("video.bin", new Uint8Array(4 * K))], null)).toMatch(/^Load the Apple IIe ROM first/);
    const c = { rom: fakeIIeRom(), chars: null, diskBoot: null, diskSequencer: null };
    expect(takePart("chars", [file("x.bin", new Uint8Array(100))], c)).toBe("x.bin: 100 bytes is no character ROM (want 4 or 8 KB)");
    expect(takePart("diskBoot", [file("p6.bin", seq())], c)).toMatch(/^p6\.bin: not a Disk II boot ROM/);
    expect(takePart("diskSequencer", [file("p5.bin", boot())], c)).toBe("p5.bin: that's the Disk II boot ROM (341-0027), not the sequencer");
    expect(takePart("diskBoot", [file("p5.bin", new Uint8Array(512))], c)).toBe("p5.bin: 512 bytes is no Disk II PROM (want 256)");
    expect(takePart("diskBoot", [file("a", boot()), file("b", boot())], c)).toBe("One file, please");
    const b = takePart("diskBoot", [file("p5.bin", boot())], c);
    if (typeof b === "string") throw new Error(b);
    const s = takePart("diskSequencer", [file("p6.bin", seq())], b);
    if (typeof s === "string") throw new Error(s);
    expect(s.diskBoot).toEqual(boot());
    expect(s.diskSequencer).toEqual(seq());
    expect(s.rom).toBe(c.rom);
  });

  test("taking a part out; the IIe ROM out is the whole cartridge out", () => {
    const c = { rom: fakeIIeRom(), chars: new Uint8Array(4 * K), diskBoot: boot(), diskSequencer: seq() };
    expect(withoutPart("chars", c)).toEqual({ ...c, chars: null });
    expect(withoutPart("diskBoot", c)?.diskSequencer).toBe(c.diskSequencer);
    expect(withoutPart("rom", c)).toBeNull();
    expect(withoutPart("chars", null)).toBeNull();
  });
});
