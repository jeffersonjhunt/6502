/**
 * The monitor's input select, Terminal or Display, and how it follows the
 * machine. Terminal is the serial line (the Text scene); Display is the
 * video card (the VGA scene, or the Apple II scene in Apple II mode).
 *
 * The machine asks for Display whenever the video card shows anything: its
 * boot splash at power-on, then the BIOS's screen, a program's picture, or
 * Apple II mode. So it boots to Display, as a real OO-1 does to its monitor,
 * and stays there. It asks for Terminal with no video card, or with the
 * card's display off. ScreenFollow switches when what the machine asks for
 * changes, and only then: a choice made by hand holds until it does. Only
 * that choice is remembered for next time, and only until the machine next
 * switches; the splash at power-on is such a switch, so every visit starts
 * on Display.
 */

import type { FrameState } from "../port/machine-port";

export type Screen = "terminal" | "display";

const DISPLAY = 0x01; // CTRL: the display on
const PLAYING = 0x01; // SPLASH: the splash covers the screen

/** The screen the machine asks for in this frame. */
export function machineScreen(f: Pick<FrameState, "apple" | "videoCard">): Screen {
  if (f.apple) return "display";
  const v = f.videoCard;
  return v && (v.ctrl & DISPLAY || v.splash & PLAYING) ? "display" : "terminal";
}

/** Where the user's pick is remembered (prefs). */
export interface ScreenMemory {
  keep(screen: Screen): void;
  forget(): void;
}

export class ScreenFollow {
  private wanted: Screen;

  /** `screen`: the one showing (the pick kept from last time); `wanted`: what the machine asks for to start with. */
  constructor(
    public screen: Screen,
    wanted: Screen = "terminal",
    private readonly memory?: ScreenMemory,
  ) {
    this.wanted = wanted;
  }

  /** What the machine asks for now. @returns true if the screen changed. */
  machine(want: Screen): boolean {
    if (want === this.wanted) return false;
    this.wanted = want;
    this.memory?.forget(); // a pick holds until now
    if (this.screen === want) return false;
    this.screen = want;
    return true;
  }

  /** Picked by hand: it holds until the machine's video mode next changes, and is remembered till then. */
  pick(screen: Screen): void {
    this.screen = screen;
    this.memory?.keep(screen);
  }

  /** Switched for the machine, ahead of the frame that would say so (Start Apple IIe): not remembered. */
  follow(screen: Screen): void {
    this.screen = screen;
    this.memory?.forget();
  }
}
