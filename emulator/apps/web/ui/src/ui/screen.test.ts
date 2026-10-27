import { describe, expect, test } from "vitest";
import { biosScreen, machineScreen, ScreenFollow } from "./screen";

const BIOS = { ctrl: 0x03, layer0: 0x80, map0: 8 };

describe("what the machine asks the monitor for", () => {
  test("the BIOS's text screen is the terminal's; a program's picture is the display's", () => {
    expect(biosScreen(BIOS)).toBe(true);
    expect(machineScreen({ apple: false, videoCard: BIOS })).toBe("terminal");
    expect(machineScreen({ apple: false, videoCard: null })).toBe("terminal"); // no card, or power off
    expect(machineScreen({ apple: false, videoCard: { ...BIOS, ctrl: 0 } })).toBe("terminal"); // display off
    expect(machineScreen({ apple: false, videoCard: { ctrl: 0x0f, layer0: 0x22, map0: 20 } })).toBe("display"); // C803R's demo
    expect(machineScreen({ apple: false, videoCard: { ...BIOS, ctrl: 0x07 } })).toBe("display"); // a second layer over the text
    expect(machineScreen({ apple: false, videoCard: { ...BIOS, ctrl: 0x0b } })).toBe("display"); // sprites over it
    expect(machineScreen({ apple: false, videoCard: { ...BIOS, layer0: 0x01 } })).toBe("display"); // 40 columns
    expect(machineScreen({ apple: false, videoCard: { ...BIOS, map0: 16 } })).toBe("display"); // a text screen of its own
    expect(machineScreen({ apple: true, videoCard: { ...BIOS, ctrl: 0 } })).toBe("display"); // Apple II mode
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

  test("a screen kept from last time holds while the machine boots to the BIOS", () => {
    const s = new ScreenFollow("display");
    expect(s.machine("terminal")).toBe(false);
    expect(s.screen).toBe("display");
  });
});
