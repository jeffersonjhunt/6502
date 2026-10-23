import { expect, test } from "vitest";
import { keyBytes, pasteBytes } from "./keyboard";

const key = (k: string, mods: Partial<{ ctrlKey: boolean; altKey: boolean; metaKey: boolean }> = {}) => ({
  key: k,
  ctrlKey: false,
  altKey: false,
  metaKey: false,
  ...mods,
});
const arr = (b: Uint8Array | null) => (b ? Array.from(b) : null);

test("Enter is CR, not LF: the Woz Monitor ends a line on CR", () => {
  expect(arr(keyBytes(key("Enter"), true))).toEqual([0x0d]);
});
test("Backspace, Escape and Tab", () => {
  expect(arr(keyBytes(key("Backspace"), true))).toEqual([0x08]);
  expect(arr(keyBytes(key("Escape"), true))).toEqual([0x1b]);
  expect(arr(keyBytes(key("Tab"), true))).toEqual([0x09]);
});
test("CAPS raises letters only", () => {
  expect(arr(keyBytes(key("a"), true))).toEqual([0x41]);
  expect(arr(keyBytes(key("a"), false))).toEqual([0x61]);
  expect(arr(keyBytes(key("."), true))).toEqual([0x2e]);
});
test("Ctrl+letter is its control code", () => {
  expect(arr(keyBytes(key("c", { ctrlKey: true }), true))).toEqual([0x03]);
  expect(arr(keyBytes(key("[", { ctrlKey: true }), true))).toEqual([0x1b]);
});
test("browser shortcuts, function keys and non-ASCII are not taken", () => {
  expect(keyBytes(key("v", { metaKey: true }), true)).toBeNull();
  expect(keyBytes(key("x", { altKey: true }), true)).toBeNull();
  expect(keyBytes(key("F5"), true)).toBeNull();
  expect(keyBytes(key("ArrowUp"), true)).toBeNull();
  expect(keyBytes(key("é"), true)).toBeNull();
});
test("paste: line ends become CR, letters follow CAPS, the rest is dropped", () => {
  expect(arr(pasteBytes("c680\r\nff00.ff0f\nx é", true))).toEqual(
    Array.from("C680\rFF00.FF0F\rX", (c) => c.charCodeAt(0)),
  );
});
