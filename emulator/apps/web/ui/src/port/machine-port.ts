/**
 * The UI's only way to the machine (emulator/docs/MACHINE_PORT.md).
 *
 * Everything here is something the real board's ESP32 bridge can also do:
 * physical panel inputs, the Console card's registers over its SPI header,
 * the serial line, and reads that take no bus cycle. So the WebAssembly
 * emulator (WasmPort) and, later, a WebSocket to a native emulator or the
 * board itself are interchangeable; `caps` says what a backend cannot do
 * (the board cannot show A/X/Y, for one).
 *
 * Calls that only send are fire-and-forget; reads are async, because over
 * a socket they are. State arrives in a FrameState about once per
 * animation frame.
 *
 * Apple II mode needs the owner's Apple IIe ROM in the cartridge
 * (insertCartridge): the page never ships one. A backend takes it as the
 * board takes a cartridge, and keeps it no further than its own memory.
 */

export interface PortCaps {
  /** CPU registers can be read (not on the real board). */
  registers: boolean;
  /** A ring of recent bus cycles can be read. */
  trace: boolean;
  /** CONT can step a single bus cycle (S BUS CYCLE); the emulator steps instructions. */
  busCycleStep: boolean;
  /** FrameState.video carries the video card's frames (watchVideo). */
  video: boolean;
  /** sendKey and sendMouse reach a USB card. */
  usb: boolean;
  /** setPower() works: the keyswitch. */
  power: boolean;
  /** insertCartridge() works: the owner's IIe ROM, for Apple II mode. */
  cartridge: boolean;
}

/** The owner's Apple IIe ROM for the cartridge (ROM B). */
export interface Cartridge {
  /** 16 KB, as it sits at $C000-$FFFF. */
  rom: Uint8Array;
  /** The IIe's character ROM, 4 or 8 KB; null: the BIOS makes one from the VGA font. */
  chars: Uint8Array | null;
}

/** Apple II mode's soft switches (hw/design.md, "Apple II Mode"), and whether the mode is on. */
export interface AppleState {
  /** The north bridge's MODE: the Apple's map and clock. */
  mode: boolean;
  text: boolean;
  mixed: boolean;
  page2: boolean;
  hires: boolean;
  col80: boolean;
  store80: boolean;
  altchar: boolean;
  /** AN3 off: with 80COL, the double modes. */
  dhires: boolean;
  ramrd: boolean;
  ramwrt: boolean;
  altzp: boolean;
  intcxrom: boolean;
  slotc3rom: boolean;
  intc8rom: boolean;
  /** The language card: $D000 bank 2, reads RAM, writes RAM. */
  lcBank2: boolean;
  lcRead: boolean;
  lcWrite: boolean;
  ioudis: boolean;
  /** AN0-AN3, bit n. */
  an: number;
}

export interface Registers {
  pc: number;
  a: number;
  x: number;
  y: number;
  s: number;
  p: number;
}

/** Lamp brightness, 0 to 1: the share of bus cycles each line was high, or 0/1 while halted. */
export interface Lights {
  /** A0-A19, index = bit. */
  addr: Float32Array;
  /** D0-D7, index = bit. */
  data: Float32Array;
  sync: number;
  rwb: number;
  vpb: number;
  rdy: number;
}

export interface TraceRow {
  cycle: number;
  /** A0-A19. */
  physical: number;
  /** The CPU's address. */
  cpu: number;
  data: number;
  /** Module ID 0-15, or 255 for none. */
  module: number;
  write: boolean;
  sync: boolean;
  vpb: boolean;
}

/** The video card's last finished frame: VGA 640x480, R G B A per pixel, row by row. */
export interface VideoFrame {
  width: number;
  height: number;
  /** Frames the card has finished since power on; this is that one. */
  frames: number;
  pixels: Uint8ClampedArray;
}

export interface MouseInput {
  x?: number;
  y?: number;
  buttons?: number;
  wheel?: number;
}

export type StopReason = "budget" | "halted" | "stopped";

export interface FrameState {
  /** Bus cycles since power on. */
  cycles: number;
  powered: boolean;
  /** The Console card holds the CPU (HALT switch, CTRL, a breakpoint). */
  halted: boolean;
  /** The CPU executed STP. */
  stopped: boolean;
  /** /IRQ is asserted by some device (the panel's IRQ lamp). */
  irq: boolean;
  /** Emulated speed over the last second or so, in Hz; 0 while not running. */
  hz: number;
  /** The clock the machine's mode fixes (Apple II mode's 1,020,484 Hz), which setClock() cannot change; 0: none. */
  fixedHz: number;
  /** In Apple II mode (the panel's AMODE lamp). */
  apple: boolean;
  /** What the machine sent on its serial line since the last frame. */
  serialOut: Uint8Array;
  /** Only while setLights(true). */
  lights: Lights | null;
  /** Only while watchRegisters(true) and caps.registers. */
  regs: Registers | null;
  /**
   * Only while watchVideo(true), with a video card, and only when it has
   * finished a frame since the last FrameState: draw it, keep it if needed.
   */
  video: VideoFrame | null;
}

export type PanelKey = "load" | "exam" | "dep" | "cont" | "start";

/** Native clock 7.15909 MHz; the Apple II modes run ~1.02 MHz. */
export const CLOCKS_HZ = [1_020_484, 1_789_773, 3_579_545, 7_159_090] as const;
export type Clock = number | "max";

/** Console card register offsets (hw/design.md, "Console registers"). */
export const CARD = {
  SWR: 0x00,
  PANEL: 0x03,
  KEYS: 0x04,
  DISPLAY: 0x05,
  CTRL: 0x06,
  BPADDR: 0x08,
  BPCTRL: 0x0b,
  STATUS: 0x0c,
  LADDR: 0x10,
  LDATA: 0x13,
  LSTAT: 0x14,
  PADDR: 0x15,
  IEN: 0x18,
  ISR: 0x19,
  ID: 0x7f,
} as const;

export interface MachinePort {
  readonly caps: PortCaps;

  /** Open the machine and start running. Rejects with a readable reason. */
  connect(): Promise<{ machine: string }>;
  /** Stop the frame loop and let go of the backend. */
  close(): void;

  setClock(clock: Clock): void;
  /** The keyswitch: off stops everything; on is a cold start (RAM forgets). */
  setPower(on: boolean): void;

  /** The 20 switch-register switches. */
  setSwitches(swr: number): void;
  /** HALT/ENABLE (true = HALT) and S INST/S BUS CYCLE (true = bus cycle). */
  setPanel(panel: { halt: boolean; busCycle: boolean }): void;
  /** The keyswitch at LOCK. */
  setLock(locked: boolean): void;
  press(key: PanelKey): void;

  /** The card's SPI header. */
  cardRead(offset: number): Promise<number>;
  cardWrite(offset: number, value: number): void;

  /** Bytes to the machine's serial line, each offered until taken. */
  sendSerial(bytes: Uint8Array): void;
  /** The USB keyboard: a HID usage code (hw/design.md, "USB card registers") pressed or released. */
  sendKey(usage: number, down: boolean): void;
  /** The USB mouse: an absolute position (0-639, 0-479), buttons (bit 0 left, 1 right, 2 middle), wheel steps. */
  sendMouse(mouse: MouseInput): void;

  /**
   * Memory without bus cycles: CPU addresses (through the windows), physical,
   * or "io": the cards' registers at the CPU addresses the native map gives
   * them ($C000-$CFFF), in Apple II mode too, where the CPU sees the Apple's.
   */
  readMemory(addr: number, length: number, space: "cpu" | "physical" | "io"): Promise<Uint8Array>;
  /** What A0-A19 carry for a CPU address now. */
  physical(cpuAddr: number): Promise<number>;
  /** A label or hex ("C800", "$C800") to a CPU address; null if neither. */
  resolve(text: string): Promise<number | null>;

  /**
   * Plug the owner's IIe ROM into the cartridge, or take it out (null), as
   * with the power off: the machine starts afresh. Rejects with a readable
   * reason if it does not fit; the old cartridge stays.
   */
  insertCartridge(cartridge: Cartridge | null): Promise<void>;
  /** The Apple's soft switches; null on a machine without the backplane's north bridge. */
  readApple(): Promise<AppleState | null>;

  setLights(on: boolean): void;
  watchRegisters(on: boolean): void;
  /** Send the video card's frames (FrameState.video); off, they cost nothing. */
  watchVideo(on: boolean): void;
  setTrace(on: boolean, depth?: number): void;
  /** Up to `max` of the newest cycles, oldest first. */
  readTrace(max: number): Promise<TraceRow[]>;

  /** Called about once per animation frame. @returns unsubscribe */
  onFrame(listener: (frame: FrameState) => void): () => void;
}
