/**
 * The monitor's input select, Terminal or Display, and how it follows the
 * machine. Terminal is the serial line (the Text scene); Display is the
 * video card (the VGA scene, or the Apple II scene in Apple II mode).
 *
 * The machine asks for Display when it is in Apple II mode, or when the
 * video card shows a picture of a program's own. The BIOS's own text
 * screen on the card (layer 0 alone, text 80x30 at the map VINIT puts it
 * at) is the serial session mirrored, so it counts as Terminal's, as does
 * the card's display off. ScreenFollow switches when what the machine asks
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
const BIOS_CONFIG = 0x80; // text 80x30, attribute bit 7 blinks (backplane/bios/video.s, VSCREEN)
const BIOS_MAP = 8; // TEXT_UNIT: $82000

/** The video card shows the BIOS's own text screen, and nothing else. */
export function biosScreen(v: VideoRegs): boolean {
  return (v.ctrl & DISPLAY) !== 0 && (v.ctrl & LAYERS) === LAYER0_ONLY && v.layer0 === BIOS_CONFIG && v.map0 === BIOS_MAP;
}

/** The screen the machine asks for in this frame. */
export function machineScreen(f: Pick<FrameState, "apple" | "videoCard">): Screen {
  if (f.apple) return "display";
  const v = f.videoCard;
  if (!v || !(v.ctrl & DISPLAY) || biosScreen(v)) return "terminal";
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
