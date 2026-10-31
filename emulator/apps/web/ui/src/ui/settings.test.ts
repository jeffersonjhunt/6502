// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { h } from "./dom";
import { settingsPanel } from "./settings";

function make() {
  const joysticks = h("div", { class: "row-wrap" }, "the sticks");
  const s = settingsPanel({ roms: [], drives: [], sd: h("div", {}), sound: h("div", {}), joysticks, phosphor: h("div", {}) });
  document.body.replaceChildren(s.element);
  const section = s.element.querySelector<HTMLElement>('[data-testid="settings-joysticks"]')!;
  return { s, section };
}

describe("the Settings drawer", () => {
  test("Joysticks, heading and all, is hidden until the machine says it has the ports", () => {
    const { s, section } = make();
    expect(section.hidden).toBe(true);
    expect(s.at("joysticks").closest("[hidden]")).toBe(section); // the heading with it
    s.show("joysticks", true);
    expect(section.hidden).toBe(false);
    expect(s.at("joysticks").closest("[hidden]")).toBe(null);
    s.show("joysticks", false); // a machine without them
    expect(section.hidden).toBe(true);
  });

  test("the other sections are not hidden by it", () => {
    const { s } = make();
    for (const id of ["roms", "disk", "sd", "sound", "display", "about"] as const) expect(s.at(id).closest("[hidden]")).toBe(null);
  });
});
