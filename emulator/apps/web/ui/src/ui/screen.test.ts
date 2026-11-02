import { describe, expect, test } from "vitest";
import { machineScreen, ScreenFollow } from "./screen";

const BIOS = { ctrl: 0x03, splash: 0 };

describe("what the machine asks the monitor for", () => {
  test("anything on the video card is the display's, as a real monitor shows it", () => {
    expect(machineScreen({ apple: false, videoCard: { ctrl: 0, splash: 1 } })).toBe("display"); // the boot splash, before VINIT
    expect(machineScreen({ apple: false, videoCard: { ...BIOS, splash: 1 } })).toBe("display"); // the splash over the BIOS's screen
    expect(machineScreen({ apple: false, videoCard: BIOS })).toBe("display"); // the BIOS's screen after it
    expect(machineScreen({ apple: false, videoCard: { ctrl: 0x0f, splash: 0 } })).toBe("display"); // C803R's demo
    expect(machineScreen({ apple: true, videoCard: { ctrl: 0, splash: 0 } })).toBe("display"); // Apple II mode
  });

  test("the terminal's with no video card, power off, or the card's display off", () => {
    expect(machineScreen({ apple: false, videoCard: null })).toBe("terminal");
    expect(machineScreen({ apple: false, videoCard: { ctrl: 0, splash: 0 } })).toBe("terminal");
    expect(machineScreen({ apple: false, videoCard: { ctrl: 0x02, splash: 0 } })).toBe("terminal"); // a layer on, the display off
  });
});

describe("following the machine", () => {
  test("switches when what the machine asks for changes, both ways", () => {
    const s = new ScreenFollow("terminal");
    expect(s.machine("terminal")).toBe(false);
    expect(s.machine("display")).toBe(true);
    expect(s.screen).toBe("display");
    expect(s.machine("display")).toBe(false);
    expect(s.machine("terminal")).toBe(true);
    expect(s.screen).toBe("terminal");
  });

  test("a choice by hand holds until the machine's mode next changes", () => {
    const s = new ScreenFollow("terminal");
    s.machine("display");
    s.pick("terminal"); // the demo runs, but the user wants the terminal
    expect(s.machine("display")).toBe(false);
    expect(s.screen).toBe("terminal");
    expect(s.machine("terminal")).toBe(false); // the demo ends: already there
    s.pick("display"); // the BIOS's screen on VGA, by hand
    expect(s.machine("terminal")).toBe(false);
    expect(s.screen).toBe("display");
    expect(s.machine("display")).toBe(false); // the machine catches up: nothing to do
    expect(s.machine("terminal")).toBe(true); // and back
    expect(s.screen).toBe("terminal");
  });

  test("a terminal kept from last time gives way to the splash at power-on, and Display stays", () => {
    const s = new ScreenFollow("terminal");
    expect(s.machine("terminal")).toBe(false); // power on: the card not set up yet
    expect(s.machine("display")).toBe(true); // the splash
    expect(s.screen).toBe("display");
    expect(s.machine("display")).toBe(false); // the BIOS's screen after it: no change
    expect(s.screen).toBe("display");
  });

  test("only a pick by hand is remembered, and only until the machine next switches", () => {
    const kept: (string | null)[] = [];
    const memory = { keep: (s: string) => kept.push(s), forget: () => kept.push(null) };
    const s = new ScreenFollow("terminal", "terminal", memory);
    s.pick("display");
    expect(kept).toEqual(["display"]);
    s.machine("terminal"); // nothing new from the machine: the pick stands
    expect(kept).toEqual(["display"]);
    s.machine("display"); // the demo: its switch is not the user's
    s.machine("terminal");
    expect(kept.at(-1)).toBeNull();
    s.follow("display"); // Start Apple IIe: the machine's, ahead of its frame
    expect(s.screen).toBe("display");
    expect(kept.at(-1)).toBeNull();
    expect(kept.filter((k) => k !== null)).toEqual(["display"]);
  });
});
