/**
 * The OO-1 workbench page (mockup C): the toolbar, the CRT monitor with the
 * serial terminal on it, the front panel in its rack, and the debugger
 * pane beside them. Everything talks to the machine through one MachinePort.
 *
 * The monitor's scenes: Text (the serial terminal), VGA (the video card)
 * and Apple II (the video card's IIe picture, filling the screen). Entering
 * Apple II mode, however it happens, switches to the Apple II scene; under
 * the monitor are the cartridge and the IIe's controls (ui/apple-bar.ts),
 * the Disk II's drive, the SD card's slot (ui/sd-bar.ts), and Sound, the
 * sound card through the speakers, with a .sid tune to play on it
 * (ui/sid-bar.ts).
 *
 * Built with VITE_HOME_URL set, the badge links back to that page.
 */

import "@fontsource/inter/400.css";
import "@fontsource/inter/500.css";
import "@fontsource/inter/600.css";
import "@fontsource/inter/800.css";
import "@fontsource/jetbrains-mono/400.css";
import "@fontsource/jetbrains-mono/600.css";
import "@fontsource/vt323/400.css";
import "./styles/base.css";
import "./styles/crt.css";
import "./styles/panel.css";
import "./styles/debugger.css";

import { Crt, PHOSPHORS, type Scene } from "./crt/crt";
import { isPaste, keyBytes, leavesScreen, pasteBytes } from "./crt/keyboard";
import { typeText, UsbKeyboard } from "./crt/usb-keyboard";
import { Terminal } from "./crt/terminal";
import type { FrameState, MachinePort } from "./port/machine-port";
import { WasmPort } from "./port/wasm-port";
import { focusStop, h, prefs, segmented } from "./ui/dom";
import { debuggerPane } from "./debugger/pane";
import { frontPanel } from "./panel/panel";
import { PanelState } from "./ui/panel-state";
import { toolbar } from "./ui/toolbar";
import { appleBar } from "./ui/apple-bar";
import { diskBar } from "./ui/disk-bar";
import { browserDiskStore } from "./ui/disk-store";
import { Speaker } from "./audio/speaker";
import { soundButton } from "./ui/sound-button";
import { sdBar } from "./ui/sd-bar";
import { sidBar } from "./ui/sid-bar";

const TERMINAL_LABEL = "Terminal: click and type. F6 leaves the screen";

function statusText(f: FrameState, clockLabel: string): string {
  if (!f.powered) return "POWER OFF";
  if (f.stopped) return "STOPPED (STP)";
  if (f.halted) return "HALTED";
  return `RUNNING · ${clockLabel}`;
}

/**
 * The terminal reads a keyboard; a touch screen brings up none for it. Say
 * so on a device with no fine pointer, once, rather than look broken.
 */
function touchNote(): HTMLElement[] {
  if (!matchMedia("(pointer: coarse)").matches || matchMedia("(any-pointer: fine)").matches) return [];
  if (prefs.get("touch-note") === "seen") return [];
  const ok = h("button", { class: "tbtn", type: "button" }, "OK");
  const note = h(
    "p",
    { class: "touch-note", role: "note", "data-testid": "touch-note" },
    "The terminal needs a keyboard: on a tablet, connect one. The front panel and the debugger work by touch.",
    ok,
  );
  ok.addEventListener("click", () => {
    prefs.set("touch-note", "seen");
    note.remove();
  });
  return [note];
}

function start(root: HTMLElement, port: MachinePort): void {
  const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const terminal = new Terminal();
  const panel = new PanelState(port);
  panel.restore({ swr: Number(prefs.get("swr") ?? 0) || 0 }); // the switches stay where they were left
  let key = panel.switches.key;
  panel.onChange((sw) => {
    prefs.set("swr", String(sw.swr));
    if (key === "off" && sw.key !== "off") terminal.clear(); // power on: a fresh screen
    key = sw.key;
  });

  const canvas = h("canvas", { "aria-hidden": "true" });
  const crt = new Crt(canvas, terminal, { reducedMotion });
  let caps = true;

  const dbg = debuggerPane(port);
  const speaker = new Speaker(port);
  const sound = soundButton(speaker, port.caps.audio && typeof AudioWorkletNode !== "undefined");
  const tune = sidBar(port, {
    soundOn: () => !speaker.on && (void sound.turnOn(), true),
    soundOff: () => sound.turnOff(),
  });
  const bar = toolbar(port, panel, {
    trailing: [dbg.toggle],
    phosphor: (p) => {
      crt.setPhosphor(p);
      document.documentElement.style.setProperty("--halo", PHOSPHORS[p].halo);
    },
    caps: (on) => (caps = on),
    home: (import.meta.env.VITE_HOME_URL as string | undefined) || undefined,
  });

  // The screen as text, for screen readers and tests.
  const mirror = h("pre", { class: "sr-only", role: "log", "aria-live": "polite", "data-testid": "crt-text" });
  const glass = h(
    "div",
    {
      class: "mon-glass",
      tabindex: "0",
      role: "application",
      "aria-label": TERMINAL_LABEL,
      "data-testid": "crt",
      "data-scene": "text",
    },
    canvas,
    h("div", { class: "halo" }),
    h("div", { class: "scan" }),
    h("div", { class: "vign" }),
    h("div", { class: "gl" }),
  );
  // The keyboard is the serial terminal's on the Text scene, and the USB
  // card's on VGA and Apple II: a monitor and a USB keyboard are a whole
  // computer. The mouse is the card's on VGA.
  let scene: Scene = "text";
  const usbKeys = new UsbKeyboard((usage, down) => port.sendKey(usage, down));
  const onUsb = () => scene !== "text" && port.caps.usb;
  const onMouse = () => scene === "vga" && port.caps.usb;
  // Tab and Escape are the machine's, so F6 is the way off the screen by keyboard.
  const leave = (way: 1 | -1) => {
    if (!focusStop(glass, way)) glass.blur();
  };
  glass.addEventListener("keydown", (e) => {
    const way = leavesScreen(e);
    if (way) {
      e.preventDefault();
      return leave(way);
    }
    if (isPaste(e)) return; // the browser's: its paste event types the text, below
    if (onUsb()) {
      if (usbKeys.down(e)) e.preventDefault();
      return;
    }
    const bytes = keyBytes(e, caps);
    if (!bytes) return;
    e.preventDefault();
    port.sendSerial(bytes);
  });
  glass.addEventListener("keyup", (e) => {
    if (usbKeys.up(e)) e.preventDefault();
  });
  glass.addEventListener("blur", () => usbKeys.releaseAll());
  window.addEventListener("blur", () => usbKeys.releaseAll());

  // The USB mouse, on VGA: where the pointer is on the picture, in its 640x480.
  const vgaPoint = (e: MouseEvent) => {
    const r = canvas.getBoundingClientRect();
    return { x: Math.floor(((e.clientX - r.left) / r.width) * 640), y: Math.floor(((e.clientY - r.top) / r.height) * 480) };
  };
  const BUTTONS = (b: number) => b & 7; // the DOM's left, right, middle bits are HID's
  glass.addEventListener("mousemove", (e) => {
    if (onMouse()) port.sendMouse({ ...vgaPoint(e), buttons: BUTTONS(e.buttons) });
  });
  for (const type of ["mousedown", "mouseup"] as const)
    glass.addEventListener(type, (e) => {
      if (onMouse()) port.sendMouse({ ...vgaPoint(e), buttons: BUTTONS(e.buttons) });
    });
  glass.addEventListener(
    "wheel",
    (e) => {
      if (!onMouse()) return;
      e.preventDefault();
      port.sendMouse({ wheel: Math.sign(e.deltaY) });
    },
    { passive: false },
  );
  glass.addEventListener("contextmenu", (e) => {
    if (onMouse()) e.preventDefault(); // the right button is the machine's
  });
  // A paste is typed on whichever keyboard the screen has: on the USB one,
  // its card holds each character until the machine has taken the last, so
  // a long one takes a while, and Stop typing drops the rest.
  let pasting = false;
  const stopTyping = h(
    "button",
    { class: "tbtn stop-typing", type: "button", hidden: true, title: "What the paste has not typed yet is dropped", "data-testid": "stop-typing" },
    "Stop typing",
  );
  stopTyping.addEventListener("click", () => {
    port.stopTyping();
    usbKeys.releaseAll();
    pasting = false;
    stopTyping.hidden = true;
    glass.focus();
  });
  glass.addEventListener("paste", (e) => {
    e.preventDefault();
    const text = e.clipboardData?.getData("text") ?? "";
    if (onUsb()) {
      usbKeys.releaseAll();
      typeText(text, (usage, down) => port.sendKey(usage, down));
      pasting = true;
    } else port.sendSerial(pasteBytes(text, caps));
  });

  // The video card's frames are copied out only while its scene is showing.
  const showScene = (s: Scene) => {
    if (s !== scene) usbKeys.releaseAll();
    scene = s;
    bar.setCapsEnabled(!onUsb());
    glass.setAttribute(
      "aria-label",
      !onUsb() ? TERMINAL_LABEL : `${s === "apple" ? "Apple IIe" : "VGA"} screen: click and type on the USB keyboard. F6 leaves the screen`,
    );
    crt.setScene(s);
    glass.dataset.scene = s;
    port.watchVideo(s !== "text");
    if (s === "text") crt.clearVideo();
    prefs.set("scene", s);
  };
  const savedScene = (["text", "vga", "apple"] as Scene[]).find((s) => s === prefs.get("scene")) ?? "text";
  const scenes = segmented<Scene>(
    "Scene",
    [
      { value: "text", text: "Text", title: "The serial terminal" },
      { value: "vga", text: "VGA", title: "The video card: VGA 640x480, what the BIOS puts on screen" },
      { value: "apple", text: "Apple II", title: "Apple II mode: the IIe's picture, filling the screen" },
    ],
    savedScene,
    (s) => {
      showScene(s);
      glass.focus(); // the keyboard is still the serial line's
    },
  );
  showScene(savedScene);
  const status = h("span", { class: "mon-status", "data-testid": "status" }, "STARTING");
  const signal = h("span", {}, "VGA 640×480");
  const apple = appleBar(port, {
    started: () => {
      scenes.select("apple");
      showScene("apple");
      glass.focus(); // the IIe's keyboard
    },
    typed: () => glass.focus(),
  });
  const download = (name: string, bytes: Uint8Array) => {
    const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)], { type: "application/octet-stream" }));
    const a = h("a", { href: url, download: name });
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 10_000);
  };
  const drive = diskBar(port, browserDiskStore(), download, apple.bootNeeds);
  const card = sdBar(port, download);
  const monitor = h(
    "div",
    { class: "mon" },
    glass,
    h("div", { class: "mon-foot" }, h("span", {}, h("i", {}), "OO-1 · ", signal, " · ", status, stopTyping), apple.element, scenes.element),
    h("div", { class: "mon-foot mon-drive" }, drive.element),
    h("div", { class: "mon-foot mon-sd" }, card.element),
    h("div", { class: "mon-foot mon-sound" }, sound.element, tune.element), // the monitor's speaker, and a tune for it
  );

  const rack = frontPanel(panel);
  // The BIOS's font is CC BY-SA 4.0 (backplane/bios/font): the page carries it, so it says whose it is.
  const credits = h(
    "p",
    { class: "credits", "data-testid": "credits" },
    "Screen font: IBM VGA 8×16 from ",
    h("a", { href: "https://int10h.org/oldschool-pc-fonts/", target: "_blank", rel: "noopener" }, "VileR's Ultimate Oldschool PC Font Pack"),
    ", ",
    h("a", { href: "https://creativecommons.org/licenses/by-sa/4.0/", target: "_blank", rel: "noopener" }, "CC BY-SA 4.0"),
    ". Apple II mode runs your own Apple IIe ROM and disks: they stay in this browser and are never uploaded. So does an SD card image, which stays in this page.",
  );
  const bench = h("section", { class: "bench" }, ...touchNote(), monitor, rack.element, credits, mirror);
  root.append(bar.element, h("main", { class: "wrap" }, bench, dbg.element), dbg.reopen);

  let clockLabel = "7.16 MHz";
  let mirrored = "";
  let lastMirror = 0;
  let wasPowered = true;
  let wasApple = false;
  port.onFrame((f) => {
    if (f.apple !== wasApple) {
      wasApple = f.apple;
      crt.setApple(f.apple);
      signal.textContent = f.apple ? "APPLE IIe" : "VGA 640×480";
      if (f.apple && scene !== "apple") {
        scenes.select("apple"); // C806R typed, or Start Apple IIe: the Apple's picture
        showScene("apple");
      }
    }
    if (pasting && !f.keysQueued) pasting = false;
    const typing = pasting && f.keysQueued >= 40; // not for a line or two
    if (stopTyping.hidden === typing) stopTyping.hidden = !typing; // set only on a change: no restyle a frame
    apple.update(f);
    drive.update(f);
    card.update(f);
    speaker.feed(f);
    sound.update();
    tune.update(f);
    if (f.serialOut.length) terminal.write(f.serialOut);
    if (f.video) crt.setVideo(f.video);
    else if (!f.powered && wasPowered) crt.clearVideo(); // the monitor loses its signal
    wasPowered = f.powered;
    crt.setPowered(f.powered);
    bar.update(f);
    rack.update(f);
    dbg.update(f);
    if (f.hz > 0) clockLabel = `${(f.hz / 1e6).toFixed(2)} MHz`;
    status.textContent = statusText(f, clockLabel);
    const now = performance.now();
    crt.draw(now);
    if (now - lastMirror > 250) {
      lastMirror = now;
      const text = terminal.text();
      if (text !== mirrored) mirror.textContent = mirrored = text;
    }
  });

  // The core threw: the machine is gone until a reload, and the screen says so rather than freeze on RUNNING.
  port.onFail((why) => {
    status.textContent = "CRASHED";
    terminal.write(new TextEncoder().encode(`\r\rThe emulator stopped: ${why}\rReload the page to start it again.\r`));
    crt.draw(performance.now());
    mirror.textContent = mirrored = terminal.text();
  });

  // Keep the canvas's pixels matched to the glass on this display; a move
  // to another display, or zooming, fires resize too.
  const fit = () => crt.resize(canvas.getBoundingClientRect().width, devicePixelRatio || 1);
  new ResizeObserver(fit).observe(canvas);
  window.addEventListener("resize", fit);
  void document.fonts.load("20px VT323").then(() => crt.fontsReady());
  port
    .connect()
    .then(async () => {
      await apple.restore(); // a power cycle, if a cartridge was kept: before the panel's state goes out
      panel.applyAll();
      port.setLights(true);
      sound.restore(); // left on last time: it plays from the first click or key
      dbg.connect();
      glass.focus();
      await drive.restore(); // the disk kept from last time, which stays in through power cycles: last, as IndexedDB can be slow to answer
    })
    .catch((err: unknown) => {
      status.textContent = "NO MACHINE";
      terminal.write(new TextEncoder().encode(`\r${String(err instanceof Error ? err.message : err)}\r`));
      crt.draw(performance.now());
      mirror.textContent = terminal.text();
    });
}

start(document.getElementById("app")!, new WasmPort());
