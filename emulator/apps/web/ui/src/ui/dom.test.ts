// @vitest-environment happy-dom
import { expect, test } from "vitest";
import { focusStop } from "./dom";

test("focusStop: the next stop Tab would take, past ones it skips or that won't take focus", () => {
  document.body.innerHTML = `<button id="before">b</button><div id="screen" tabindex="0"></div>
    <button id="minus" tabindex="-1">-1</button><div inert><button id="inert">i</button></div>
    <button id="stuck">s</button><button id="after">a</button>`;
  const $ = (id: string) => document.getElementById(id)!;
  $("stuck").focus = () => {}; // visibility: hidden, say
  $("screen").focus();
  expect(focusStop($("screen"), 1)).toBe(true);
  expect(document.activeElement?.id).toBe("after");
  $("screen").focus();
  expect(focusStop($("screen"), -1)).toBe(true);
  expect(document.activeElement?.id).toBe("before");
  expect(focusStop($("after"), 1)).toBe(false); // the last: nothing after it
});
