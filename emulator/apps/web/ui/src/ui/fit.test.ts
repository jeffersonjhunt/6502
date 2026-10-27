import { expect, test } from "vitest";
import { fitScale } from "./fit";

test("the panel shrinks to the room there is, and never grows", () => {
  expect(fitScale(900, 450)).toBe(0.5);
  expect(fitScale(900, 1200)).toBe(1);
  expect(fitScale(900, 900)).toBe(1);
  expect(fitScale(0, 300)).toBe(1); // not laid out yet
  expect(fitScale(900, 0)).toBe(1);
});
