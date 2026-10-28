/**
 * The monitor's input select, Terminal or Display, and how it follows the
 * machine. Terminal is the serial line (the Text scene); Display is the
 * video card (the VGA scene, or the Apple II scene in Apple II mode).
 *
 * The machine asks for Display when it is in Apple II mode, or when the
 * video card shows a picture of a program's own. A text screen on the
 * card (layer 0 alone, in a text mode) is the BIOS's serial session
 * mirrored, whoever's BIOS set it up and however, so it counts as
 * Terminal's, as does the card's display off. ScreenFollow switches when what the machine asks
 * for changes, and only then: a choice made by hand holds until it does.
 * Only that choice is remembered for next time, and only until the machine
 * next switches: a visit starts on the user's pick, or the terminal the
 * machine boots to, never on a switch the machine made last time.
 */

import type { FrameState, VideoRegs } from "../port/machine-port";

export type Screen = "terminal" | "display";

const DISPLAY = 0x01; // CTRL: the display on
const LAYERS = 0x0e; // CTRL: layer 0, layer 1, sprites
const LAYER0_ONLY = 0x02;
const TYPE = 0x07; // CONFIG: the layer's type
const TEXT_40 = 1; // text 80x30 is 0, 40x30 is 1; tiles and bitmaps above

/** The video card shows a text screen on layer 0, and nothing else: a BIOS's, any BIOS's. */
export function textScreen(v: VideoRegs): boolean {
  return (v.ctrl & DISPLAY) !== 0 && (v.ctrl & LAYERS) === LAYER0_ONLY && (v.layer0 & TYPE) <= TEXT_40;
}

/** The screen the machine asks for in this frame. */
export function machineScreen(f: Pick<FrameState, "apple" | "videoCard">): Screen {
  if (f.apple) return "display";
  const v = f.videoCard;
  if (!v || !(v.ctrl & DISPLAY) || textScreen(v)) return "terminal";
  return "display";
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
