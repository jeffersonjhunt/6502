import { expect, test } from "vitest";
import { isPaste, keyBytes, leavesScreen, pasteBytes } from "./keyboard";

type Mods = Partial<{ code: string; ctrlKey: boolean; altKey: boolean; metaKey: boolean; shiftKey: boolean; altGraph: boolean }>;
const key = (k: string, { altGraph = false, ...mods }: Mods = {}) => ({
  key: k,
  code: "",
  ctrlKey: false,
  altKey: false,
  metaKey: false,
  shiftKey: false,
  getModifierState: (m: string) => m === "AltGraph" && altGraph,
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
  expect(keyBytes(key("x", { altKey: true }), true, false)).toBeNull(); // Option+X on a Mac would be "≈"
  expect(keyBytes(key("F5"), true)).toBeNull();
  expect(keyBytes(key("ArrowUp"), true)).toBeNull();
  expect(keyBytes(key("é"), true)).toBeNull();
});
test("paste: line ends become CR, letters follow CAPS, the rest is dropped", () => {
  expect(arr(pasteBytes("c680\r\nff00.ff0f\nx é", true))).toEqual(
    Array.from("C680\rFF00.FF0F\rX", (c) => c.charCodeAt(0)),
  );
});

test("AltGr's characters are typed: @ [ ] \\ { } on a German or French layout", () => {
  // Windows reports AltGr as Ctrl+Alt as well; the character is what counts.
  expect(arr(keyBytes(key("@", { ctrlKey: true, altKey: true, altGraph: true }), true, false))).toEqual([0x40]);
  expect(arr(keyBytes(key("{", { altGraph: true }), true, false))).toEqual([0x7b]);
  expect(arr(keyBytes(key("\\", { ctrlKey: true, altKey: true, altGraph: true }), true, false))).toEqual([0x5c]);
  expect(keyBytes(key("€", { altGraph: true }), true, false)).toBeNull();
});
test("Option's characters are typed on the Mac; elsewhere Alt is the browser's", () => {
  expect(arr(keyBytes(key("@", { altKey: true }), true, true))).toEqual([0x40]); // Option+L, German Mac
  expect(arr(keyBytes(key("[", { altKey: true }), true, true))).toEqual([0x5b]); // Option+5
  expect(keyBytes(key("å", { altKey: true }), true, true)).toBeNull();
  expect(keyBytes(key("d", { altKey: true }), true, false)).toBeNull(); // Alt+D: the address bar
  expect(keyBytes(key("v", { metaKey: true, altKey: true }), true, true)).toBeNull();
});
test("paste chords are the browser's: Ctrl+V, Ctrl+Shift+V, Shift+Insert; Cmd+V on the Mac, where Ctrl+V is ^V", () => {
  expect(isPaste(key("v", { code: "KeyV", ctrlKey: true }), false)).toBe(true);
  expect(isPaste(key("V", { code: "KeyV", ctrlKey: true, shiftKey: true }), false)).toBe(true);
  expect(isPaste(key("м", { code: "KeyV", ctrlKey: true }), false)).toBe(true); // a Russian layout: the V key
  expect(isPaste(key("Insert", { code: "Insert", shiftKey: true }), false)).toBe(true);
  expect(isPaste(key("Insert", { code: "Insert", shiftKey: true }), true)).toBe(true);
  expect(isPaste(key("v", { code: "KeyV", ctrlKey: true }), true)).toBe(false);
  expect(isPaste(key("v", { code: "KeyV" }), false)).toBe(false);
  expect(isPaste(key("c", { code: "KeyC", ctrlKey: true }), false)).toBe(false);
  expect(isPaste(key("Insert", { code: "Insert" }), false)).toBe(false);
  expect(isPaste(key("Insert", { code: "Insert", shiftKey: true, ctrlKey: true }), false)).toBe(false);
});
test("F6 leaves the screen, Shift+F6 back the other way; with Ctrl, Alt or Cmd it is not ours", () => {
  expect(leavesScreen(key("F6", { code: "F6" }))).toBe(1);
  expect(leavesScreen(key("F6", { code: "F6", shiftKey: true }))).toBe(-1);
  expect(leavesScreen(key("F6", { code: "F6", ctrlKey: true }))).toBe(0);
  expect(leavesScreen(key("F6", { code: "F6", altKey: true }))).toBe(0);
  expect(leavesScreen(key("Tab", { code: "Tab" }))).toBe(0);
  expect(leavesScreen(key("Escape", { code: "Escape" }))).toBe(0);
});
