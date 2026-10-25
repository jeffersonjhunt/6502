// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { Speaker, type AudioOut } from "../audio/speaker";
import { FakePort } from "../port/fake-port";
import { prefs } from "./dom";
import { soundButton } from "./sound-button";

function out(): AudioOut {
  return { rate: 48_000, send() {}, clear() {}, resume: async () => {}, suspend: async () => {}, setVolume() {}, onStats() {} };
}

async function settle() {
  for (let i = 0; i < 5; ++i) await Promise.resolve();
}

function make(available = true) {
  const port = new FakePort({ audio: true });
  const speaker = new Speaker(port, async () => out());
  const b = soundButton(speaker, available);
  document.body.replaceChildren(b.element);
  return { port, speaker, b, el: b.element as HTMLButtonElement };
}

describe("sound button", () => {
  beforeEach(() => {
    prefs.set("sound", "off");
    document.body.replaceChildren();
  });

  test("a click turns it on and off, says so, and remembers", async () => {
    const { port, speaker, el } = make();
    expect(el.getAttribute("aria-pressed")).toBe("false");
    el.click();
    await settle();
    expect(speaker.on).toBe(true);
    expect(el.classList.contains("on")).toBe(true);
    expect(el.getAttribute("aria-pressed")).toBe("true");
    expect(port.callsTo("listen")).toEqual([[48_000]]);
    expect(prefs.get("sound")).toBe("on");
    el.click();
    expect(speaker.on).toBe(false);
    expect(port.callsTo("listen").at(-1)).toEqual([0]);
    expect(prefs.get("sound")).toBe("off");
  });

  test("left on: restore turns it on; left off, or no sound to play: it stays off", async () => {
    prefs.set("sound", "on");
    let m = make();
    m.b.restore();
    await settle();
    expect(m.speaker.on).toBe(true);
    m = make(false);
    expect(m.el.disabled).toBe(true);
    m.b.restore();
    await settle();
    expect(m.speaker.on).toBe(false);
    prefs.set("sound", "off");
    m = make();
    m.b.restore();
    await settle();
    expect(m.speaker.on).toBe(false);
  });

  test("the meter shows the peak while on", async () => {
    const { speaker, b, el } = make();
    el.click();
    await settle();
    speaker.peak = 0.125;
    b.update();
    expect(el.dataset.peak).toBe("0.1250");
    expect((el.querySelector(".meter") as HTMLElement).style.getPropertyValue("--level")).toBe("0.500");
    el.click();
    b.update();
    expect((el.querySelector(".meter") as HTMLElement).style.getPropertyValue("--level")).toBe("0.000");
  });
});
