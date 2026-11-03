import { describe, expect, test } from "vitest";
import { machineSource, ScreenFollow } from "./screen";

const BIOS = { ctrl: 0x03, splash: 0 };

describe("where the machine's picture comes from", () => {
  test("anything on the video card is VGA, as a real monitor shows it; Apple II mode is the Apple's", () => {
    expect(machineSource({ apple: false, videoCard: { ctrl: 0, splash: 1 } })).toBe("vga"); // the boot splash, before VINIT
    expect(machineSource({ apple: false, videoCard: { ...BIOS, splash: 1 } })).toBe("vga"); // the splash over the BIOS's screen
    expect(machineSource({ apple: false, videoCard: BIOS })).toBe("vga"); // the BIOS's screen after it
    expect(machineSource({ apple: false, videoCard: { ctrl: 0x0f, splash: 0 } })).toBe("vga"); // C803R's demo
    expect(machineSource({ apple: true, videoCard: { ctrl: 0, splash: 0 } })).toBe("apple"); // Apple II mode
  });

  test("the serial line alone with no video card, power off, or the card's display off", () => {
    expect(machineSource({ apple: false, videoCard: null })).toBe("terminal");
    expect(machineSource({ apple: false, videoCard: { ctrl: 0, splash: 0 } })).toBe("terminal");
    expect(machineSource({ apple: false, videoCard: { ctrl: 0x02, splash: 0 } })).toBe("terminal"); // a layer on, the display off
  });
});

describe("following the machine", () => {
  test("switches when what the machine asks for changes, both ways", () => {
    const s = new ScreenFollow("terminal");
    expect(s.machine("terminal")).toBe(false);
    expect(s.machine("vga")).toBe(true);
    expect(s.screen).toBe("display");
    expect(s.machine("vga")).toBe(false);
    expect(s.machine("terminal")).toBe(true);
    expect(s.screen).toBe("terminal");
  });

  test("a choice by hand holds until the machine's source next changes", () => {
    const s = new ScreenFollow("terminal");
    s.machine("vga"); // the BIOS's screen on the card
    s.pick("terminal"); // but the user wants the terminal
    expect(s.machine("vga")).toBe(false);
    expect(s.screen).toBe("terminal");
    expect(s.machine("terminal")).toBe(false); // the display turned off: already there
    s.pick("display"); // the blank card, by hand
    expect(s.machine("terminal")).toBe(false);
    expect(s.screen).toBe("display");
    expect(s.machine("vga")).toBe(false); // the display on again: the machine catches up, nothing to do
    expect(s.machine("terminal")).toBe(true); // and off
    expect(s.screen).toBe("terminal");
  });

  test("a terminal kept from last time gives way to the splash at power-on, and Display stays", () => {
    const s = new ScreenFollow("terminal");
    expect(s.machine("terminal")).toBe(false); // power on: the card not set up yet
    expect(s.machine("vga")).toBe(true); // the splash
    expect(s.screen).toBe("display");
    expect(s.machine("vga")).toBe(false); // the BIOS's screen after it: no change
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
    s.machine("vga"); // the splash: its switch is not the user's
    s.machine("terminal");
    expect(kept.at(-1)).toBeNull();
    s.follow("display"); // Start Apple IIe: the machine's, ahead of its frame
    expect(s.screen).toBe("display");
    expect(kept.at(-1)).toBeNull();
    expect(kept.filter((k) => k !== null)).toEqual(["display"]);
  });

  test("Apple II mode is a switch of its own: it takes a Terminal picked by hand to Display", () => {
    const s = new ScreenFollow("terminal");
    s.machine("vga"); // the splash, then the BIOS's screen
    s.pick("terminal");
    expect(s.machine("vga")).toBe(false);
    expect(s.machine("apple")).toBe(true); // C685: 1 at the monitor
    expect(s.screen).toBe("display");
    s.pick("terminal");
    expect(s.machine("apple")).toBe(false); // it holds in Apple II mode too
    expect(s.machine("terminal")).toBe(false); // a cold start: off...
    expect(s.machine("vga")).toBe(true); // ...then the splash
    expect(s.screen).toBe("display");
  });
});
