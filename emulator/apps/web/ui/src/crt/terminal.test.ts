import { describe, expect, test } from "vitest";
import { COLS, ROWS, Terminal } from "./terminal";

const bytes = (s: string) => Uint8Array.from(s, (c) => c.charCodeAt(0));
const term = (s: string) => {
  const t = new Terminal();
  t.write(bytes(s));
  return t;
};

describe("Terminal", () => {
  test("CR, LF and CRLF each end one line", () => {
    expect(term("A\rB\nC\r\nD").text()).toBe("A\nB\nC\nD");
  });
  test("an LF that is not straight after a CR is its own new line", () => {
    expect(term("A\r\r\nB\n\nC").text()).toBe("A\n\nB\n\nC");
  });
  test("bit 7 is dropped, as the Woz Monitor sets it", () => {
    expect(term("\xC8\xC9").text()).toBe("HI");
  });
  test("backspace moves left and the next character overwrites", () => {
    expect(term("ABC\bX").text()).toBe("ABX");
    expect(term("\b\bA").text()).toBe("A"); // not past column 0
  });
  test("other control characters and DEL are ignored", () => {
    expect(term("A\x07\x1b\x7fB").text()).toBe("AB");
  });
  test("ESC [H ESC [2J clears the screen and homes, as OODOS's clear (ESC E) comes on the serial line", () => {
    const t = term("one\r\ntwo\x1b[H\x1b[2JX");
    expect(t.text()).toBe("X");
    expect([t.row, t.col]).toEqual([0, 1]);
  });
  test("ESC [ A, B, C, D move the cursor, by a count, stopping at the edges", () => {
    expect(term("\x1b[3B\x1b[5CX\x1b[2AY").text()).toBe("\n      Y\n\n     X");
    const t = term("\x1b[99A\x1b[99D\x1b[200C\x1b[200B");
    expect([t.row, t.col]).toEqual([ROWS - 1, COLS - 1]);
    expect(term("AB\x1b[DZ").text()).toBe("AZ"); // no count: 1
  });
  test("ESC [ row ; col H moves there, from 1; past the edge, the last", () => {
    expect(term("\x1b[3;5HX\x1b[1;1HY\x1b[HZ").text()).toBe("Z\n\n    X");
    const t = term("\x1b[40;90H");
    expect([t.row, t.col]).toEqual([ROWS - 1, COLS - 1]);
    expect([term("\x1b[2;3f").row, term("\x1b[2;3f").col]).toEqual([1, 2]);
  });
  test("ESC [ K and ESC [ J erase to the end, 1 to the start, 2 all; the cursor stays", () => {
    expect(term("ABCDEF\x1b[1;3H\x1b[KX").text()).toBe("ABX");
    expect(term("ABCDEF\x1b[1;3H\x1b[1KX").text()).toBe("  XDEF");
    expect(term("ABCDEF\x1b[1;3H\x1b[2KX").text()).toBe("  X");
    expect(term("one\r\ntwo\r\nsix\x1b[2;2H\x1b[J").text()).toBe("one\nt");
    expect(term("one\r\ntwo\r\nsix\x1b[2;2H\x1b[1J").text()).toBe("\n  o\nsix");
  });
  test("another ESC [ sequence is dropped to its final byte; ESC alone is ignored, the next byte kept", () => {
    expect(term("A\x1b[1;31mB\x1b[?25lC").text()).toBe("ABC");
    expect(term("\x1b\\").text()).toBe("\\"); // the Woz Monitor echoes ESC, then its backslash
    expect(term("\x1bEX").text()).toBe("EX"); // VT52 isn't spoken here: OODOS sends ANSI
  });
  test("OODOS's VT52 test as its serial line has it, ANSI, draws what the screen shows (test_oodos_sys)", () => {
    const t = term(
      "\x1b[H\x1b[2J\x1b[HLine one\r\nlower case\x1b[6;11HAT 5,10\x1b[H\x1b[B\x1b[B\x1b[B\x1b[C\x1b[CX" +
        "\x1b[11;1HERASE ME TO THE END\x1b[11;6H\x1b[K\x1b[13;1Habc\x1b[D\x1b[DZ\x1b[AU" +
        "\x1b[21;1HJJJ\r\nKKK\r\nLLL\x1b[22;2H\x1b[J\x1b[15;1HAB\bC\x1b[16;1H\r        T" +
        "\x1b[17;79HWRAP\x1b[30;80H\x1b[26;1H\x07END",
    );
    const want: Record<number, string> = {
      0: "Line one", 1: "lower case", 2: "", 3: "  X", 5: "          AT 5,10", 10: "ERASE", 11: "  U",
      12: "aZc", 14: "AC", 20: "JJJ", 21: "K", 22: "", 25: "END",
      // Except a lone CR: the screen's column 0, here a new line, as the ROM ends its lines with CR alone. So the
      // tab's T is a row down, and WRAP's last two letters, which the screen wraps onto row 17, overwrite it.
      15: "", 16: "        T" + " ".repeat(69) + "WR", 17: "AP",
    };
    for (const [row, line] of Object.entries(want)) expect(t.line(Number(row))).toBe(line);
  });
  test("wraps at column 80", () => {
    const t = term("x".repeat(COLS) + "y");
    expect(t.line(0)).toBe("x".repeat(COLS));
    expect(t.line(1)).toBe("y");
  });
  test("scrolls when a new line passes row 30", () => {
    const t = term(Array.from({ length: ROWS + 2 }, (_, i) => `L${i}`).join("\r"));
    expect(t.line(0)).toBe("L2");
    expect(t.line(ROWS - 1)).toBe(`L${ROWS + 1}`);
    expect(t.scrolls).toBe(2);
  });
  test("reports the rows it changed, once", () => {
    const t = term("A\rB");
    expect(t.takeDirty().sort()).toEqual([0, 1]);
    expect(t.takeDirty()).toEqual([]);
    t.write(bytes("C"));
    expect(t.takeDirty()).toEqual([1]);
  });
});
