// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { PanelState } from "./panel-state";
import { toolbar } from "./toolbar";

function bar(home?: string) {
  const port = new FakePort();
  const tb = toolbar(port, new PanelState(port), { phosphor: () => {}, caps: () => {}, home });
  document.body.replaceChildren(tb.element);
  return tb.element;
}

describe("toolbar badge", () => {
  beforeEach(() => document.body.replaceChildren());

  test("no home: the mark and the name, no link", () => {
    const el = bar();
    expect(el.querySelector("a")).toBeNull();
    expect(el.querySelector(".ttl")?.textContent).toContain("OO-1");
  });

  test("a home: the mark and the name link back to it", () => {
    const a = bar("/projects/oo-1/")?.querySelector<HTMLAnchorElement>('[data-testid="tb-home"]');
    expect(a?.getAttribute("href")).toBe("/projects/oo-1/");
    expect(a?.querySelector("img")).not.toBeNull();
    expect(a?.querySelector(".ttl")?.textContent).toContain("OO-1");
  });
});
