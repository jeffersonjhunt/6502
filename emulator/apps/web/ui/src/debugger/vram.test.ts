// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { VIDEO, devicesView } from "./devices";
import { parseVram, VRAM_DEFAULT, vramDump, vramView } from "./vram";

const submit = (el: HTMLElement, line: string) => {
  const input = el.querySelector<HTMLInputElement>('[data-testid="vram-input"]')!;
  input.value = line;
  el.querySelector("form")!.dispatchEvent(new Event("submit", { cancelable: true }));
};
const settle = () => new Promise((r) => setTimeout(r, 0));

describe("vram: card memory", () => {
  test("a line is the terminal's: ADDR [N], or ADDR = BYTE...", () => {
    expect(parseVram("2000")).toEqual({ addr: 0x2000, count: VRAM_DEFAULT });
    expect(parseVram(" $1E0000  20 ")).toEqual({ addr: 0x1e0000, count: 20 });
    expect(parseVram("1e1000 = 12 $34 ab")).toEqual({ addr: 0x1e1000, count: VRAM_DEFAULT, bytes: Uint8Array.from([0x12, 0x34, 0xab]) });
    expect(parseVram("0 4096")).toEqual({ addr: 0, count: 4096 });
  });

  test("and anything else says why", () => {
    for (const bad of ["", "1000000", "XYZ", "10 0", "10 4097", "10 1A", "10 =", "10 2 3"])
      expect(parseVram(bad), bad).toMatch(/^ADDR \[N\]/);
    expect(parseVram("10 = 100")).toBe("bad byte '100'");
  });

  test("the dump: six-digit addresses, the bytes, what prints, wrapping at 24 bits", () => {
    const bytes = Uint8Array.from([0x41, 0x42, 0x00, 0x7f, 0x43, 0x44, 0x45, 0x46, 0x47, 0xff]);
    expect(vramDump(0x1e0000, bytes)).toBe("1E0000  41 42 00 7F 43 44 45 46  AB..CDEF\n1E0008  47 FF" + " ".repeat(20) + "G.");
    expect(vramDump(0xfffffe, Uint8Array.from([1, 2, 3]), 2)).toBe("FFFFFE  01 02  ..\n000000  03     .");
  });

  test("it shows card memory, and a write goes to the card", async () => {
    const port = new FakePort();
    port.cardMemory = new Map([[0x2000, 0x48], [0x2001, 0x49]]);
    const v = vramView(port);
    await v.refresh();
    const dump = v.element.querySelector('[data-testid="vram-dump"]')!;
    expect(dump.textContent!.split("\n")[0]).toBe("002000  48 49 00 00 00 00 00 00  HI......"); // the console's text, first
    expect(dump.textContent!.split("\n")).toHaveLength(VRAM_DEFAULT / 8);
    submit(v.element, "1FFFFE = 5A A5 77");
    await settle();
    expect(port.calls).toContainEqual(["writeCardMemory", 0x1ffffe, [0x5a, 0xa5, 0x77]]);
    expect(dump.textContent!.split("\n")[0]).toBe("1FFFFE  5A A5 FF FF FF FF FF FF  Z......."); // past 2 MB: dropped, reads $FF
    submit(v.element, "10 =");
    await settle();
    expect(v.element.querySelector('[data-testid="vram-error"]')!.textContent).toMatch(/^ADDR/);
    expect(port.calls.filter((c) => c[0] === "writeCardMemory")).toHaveLength(1);
  });

  test("the Devices tab shows it for a revision 2 card only", async () => {
    const port = new FakePort();
    port.io.set(VIDEO + 0x7f, 0x91);
    const v = devicesView(port);
    await v.refresh();
    const box = v.element.querySelector<HTMLElement>('[data-testid="dev-vram"]')!;
    expect(box.hidden).toBe(true);
    port.io.set(VIDEO + 0x7f, 0x92);
    port.cardMemory = new Map([[0x2000, 0x42]]);
    await v.refresh();
    expect(box.hidden).toBe(false);
    expect(box.querySelector('[data-testid="vram-dump"]')!.textContent).toMatch(/^002000  42 /);
    port.cardMemory.set(0x2000, 0x43); // the machine runs on: the next refresh shows it
    await v.refresh();
    expect(box.querySelector('[data-testid="vram-dump"]')!.textContent).toMatch(/^002000  43 /);
  });
});
