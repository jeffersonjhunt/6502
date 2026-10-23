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
