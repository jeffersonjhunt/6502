import { describe, expect, test } from "vitest";
import { keysFor, typeText } from "./usb-keyboard";

describe("text typed on the USB keyboard", () => {
  test("each character's key in the US layout, Shift for those that need it", () => {
    const k = keysFor('10 PRINT "A+B": REM ok\r\n?');
    const S = (usage: number) => ({ usage, shift: true });
    const N = (usage: number) => ({ usage, shift: false });
    expect(k).toEqual([
      N(0x1e), N(0x27), N(0x2c), // 1 0 space
      N(0x13), N(0x15), N(0x0c), N(0x11), N(0x17), N(0x2c), // PRINT: letters as Caps Lock has them
      S(0x34), N(0x04), S(0x2e), N(0x05), S(0x34), S(0x33), N(0x2c), // "A+B":
      N(0x15), N(0x08), N(0x10), N(0x2c), N(0x12), N(0x0e), // REM ok
      N(0x28), // CR LF: one Return
      S(0x38), // ?
    ]);
  });

  test("every shifted symbol on its key, and what has none is left out", () => {
    const k = keysFor('!@#$%^&*()_+{}|:"~<>?');
    expect(k.every((x) => x.shift)).toBe(true);
    expect(k.map((x) => x.usage)).toEqual([0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38]);
    expect(keysFor("é →\n\t")).toEqual([{ usage: 0x28, shift: false }, { usage: 0x2b, shift: false }]);
  });

  test("pressed and let go, Shift around the key", () => {
    const sent: [number, boolean][] = [];
    typeText("a:", (u, d) => sent.push([u, d]));
    expect(sent).toEqual([[0x04, true], [0x04, false], [0xe1, true], [0x33, true], [0x33, false], [0xe1, false]]);
  });
});
