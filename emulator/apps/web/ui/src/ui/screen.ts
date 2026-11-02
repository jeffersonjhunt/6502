/**
 * The monitor's input select, Terminal or Display, and how it follows the
 * machine. Terminal is the serial line (the Text scene); Display is the
 * video card (the VGA scene, or the Apple II scene in Apple II mode).
 *
 * What the machine shows comes from one of three sources: the serial line
 * alone (no video card, or the card's display off), VGA (the card showing
 * anything: its boot splash at power-on, then the BIOS's screen, or a
 * program's picture), or the Apple II. So it boots to Display, as a real
 * OO-1 does to its monitor, and stays there. ScreenFollow switches when the
 * source changes, and only then: at power-on (off, then the splash), when
 * the display is turned off or on, and on entering or leaving Apple II mode.
 * A choice made by hand holds until the next of those. Only that choice is
 * remembered for next time, and only until the machine next switches; the
 * splash at power-on is such a switch, so every visit starts on Display.
 *
 * A program that blanks the display while it sets up and turns it back on
 * (C803R's demo does) is two switches, but only if a frame is drawn while
 * it is blank: frames sample the card, so a blank shorter than one may be
 * missed, and a Terminal picked by hand may or may not give way to it.
 */

import type { FrameState } from "../port/machine-port";

export type Screen = "terminal" | "display";

/** Where the machine's picture comes from: the serial line, the video card, or the Apple II. */
export type Source = "terminal" | "vga" | "apple";

const DISPLAY = 0x01; // CTRL: the display on
const PLAYING = 0x01; // SPLASH: the splash covers the screen

/** The source the machine shows in this frame. */
export function machineSource(f: Pick<FrameState, "apple" | "videoCard">): Source {
  if (f.apple) return "apple";
  const v = f.videoCard;
  return v && (v.ctrl & DISPLAY || v.splash & PLAYING) ? "vga" : "terminal";
}

/** The screen a source is on. */
export function screenOf(source: Source): Screen {
  return source === "terminal" ? "terminal" : "display";
}

/** Where the user's pick is remembered (prefs). */
export interface ScreenMemory {
  keep(screen: Screen): void;
  forget(): void;
}

export class ScreenFollow {
  private wanted: Source;

  /** `screen`: the one showing (the pick kept from last time); `wanted`: the machine's source to start with. */
  constructor(
    public screen: Screen,
    wanted: Source = "terminal",
    private readonly memory?: ScreenMemory,
  ) {
    this.wanted = wanted;
  }

  /** The machine's source now. @returns true if the screen changed. */
  machine(source: Source): boolean {
    if (source === this.wanted) return false;
    this.wanted = source;
    this.memory?.forget(); // a pick holds until now
    const want = screenOf(source);
    if (this.screen === want) return false;
    this.screen = want;
    return true;
  }

  /** Picked by hand: it holds until the machine's source next changes, and is remembered till then. */
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
