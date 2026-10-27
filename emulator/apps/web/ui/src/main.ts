/**
 * The OO-1 workbench page: the toolbar, the CRT monitor, the status strip,
 * the front panel in its rack, and the right-hand drawer that Settings and
 * the debugger share (ui/drawer.ts). Everything talks to the machine
 * through one MachinePort.
 *
 * The monitor shows the screen and its input select: Terminal (the serial
 * terminal, the Text scene) or Display (the video card: the VGA scene, or
 * the Apple II scene in Apple II mode). It follows the machine
 * (ui/screen.ts): into Apple II mode, or a picture of a program's own on
 * the video card, and it switches to Display; back out, and to Terminal.
 * The strip under it has the machine's state and what is loaded; the
 * media themselves (the ROMs, the Disk II's drives, the SD card, a .sid
 * tune) are in Settings, with the phosphor and the credits.
 *
 * Built with VITE_HOME_URL set, the back link goes to that page.
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
import "./styles/drawer.css";

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
import { PHONE, toolbar } from "./ui/toolbar";
import { appleBar } from "./ui/apple-bar";
import { biosRow, keptBios } from "./ui/bios";
import { diskBar } from "./ui/disk-bar";
import { browserDiskStore } from "./ui/disk-store";
import { drawer } from "./ui/drawer";
import { fitWidth } from "./ui/fit";
import { machineScreen, ScreenFollow, type Screen } from "./ui/screen";
import { phosphorPicker, settingsPanel, type SettingsSection } from "./ui/settings";
import { statusStrip } from "./ui/status-strip";
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
  const phone = matchMedia(PHONE);
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
    sound: sound.element,
    caps: (on) => (caps = on),
    home: (import.meta.env.VITE_HOME_URL as string | undefined) || undefined,
  });
  const phosphor = phosphorPicker((p) => {
    crt.setPhosphor(p);
    document.documentElement.style.setProperty("--halo", PHOSPHORS[p].halo);
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
  // The keyboard is the serial terminal's on Terminal, and the USB card's on
  // Display: a monitor and a USB keyboard are a whole computer. The mouse is
  // the card's on VGA.
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
    { class: "tbtn sm stop-typing", type: "button", hidden: true, title: "What the paste has not typed yet is dropped", "data-testid": "stop-typing" },
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

  // The screen: Terminal or Display, following the machine (ui/screen.ts).
  // Display is the Apple II scene in Apple II mode, VGA otherwise.
  let apple = false;
  const saved: Screen = prefs.get("screen") === "display" ? "display" : "terminal";
  const follow = new ScreenFollow(saved);
  const sceneFor = (s: Screen): Scene => (s === "terminal" ? "text" : apple ? "apple" : "vga");
  const monLabel = h("span", { class: "lbl mon-label", "data-testid": "mon-label" });
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
    monLabel.textContent = s === "text" ? "Terminal · the serial line" : s === "apple" ? "Display · Apple IIe 560×384" : "Display · VGA 640×480";
  };
  const screens = segmented<Screen>(
    "Screen",
    [
      { value: "terminal", text: "Terminal", title: "The serial terminal" },
      { value: "display", text: "Display", title: "The video card: VGA 640x480, or the IIe's picture in Apple II mode" },
    ],
    follow.screen,
    (s) => {
      follow.pick(s);
      applyScreen();
      glass.focus(); // the keyboard back on the screen, off the switch
    },
  );
  screens.element.dataset.testid = "screen-switch";
  const applyScreen = () => {
    screens.select(follow.screen);
    prefs.set("screen", follow.screen);
    if (sceneFor(follow.screen) !== scene || !monLabel.textContent) showScene(sceneFor(follow.screen));
  };
  applyScreen();

  const appleRoms = appleBar(port, {
    started: () => {
      follow.pick("display"); // the Apple's picture, without waiting for the frame that says so
      applyScreen();
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
  const openSettings = (at: SettingsSection) => {
    const head = settings.at(at);
    drawer_.show("settings", { at: head });
    drawer_.element.scrollTop = head.offsetTop - 12; // the drawer scrolls, not the page
  };
  const store = browserDiskStore();
  const drives = [0, 1].map((d) => diskBar(port, store, download, { bootNeeds: appleRoms.bootNeeds, open: () => openSettings("disk") }, d));
  const card = sdBar(port, download, window, { open: () => openSettings("sd") });
  const bios = biosRow(() => location.reload(), {
    atRisk: () => {
      const sd = card.unsaved();
      return [
        ...(sd ? [`the SD card's writes to ${sd} aren't saved (Save keeps them)`] : []),
        ...drives.flatMap((d, i) => (d.busy() ? [`drive ${i + 1} is at work, and what it writes may be lost`] : [])),
      ];
    },
    leaving: () => card.waive(),
  });
  const settings = settingsPanel({
    roms: [bios.element, appleRoms.element],
    drives: drives.map((d) => d.element),
    sd: card.element,
    sound: tune.element,
    phosphor,
  });

  const monitor = h("div", { class: "mon" }, glass, h("div", { class: "mon-foot" }, monLabel, screens.element));
  const strip = statusStrip({
    pills: [...drives.map((d) => d.pill), card.pill, tune.pill],
    trailing: [stopTyping, appleRoms.resets],
    load: () => openSettings("roms"),
  });

  const rack = frontPanel(panel);
  const about = h("button", { class: "linkish", type: "button", "data-testid": "about-link" }, "About and credits");
  about.addEventListener("click", () => openSettings("about"));
  const note = h("p", { class: "page-note" }, "Your ROMs, disks and card images stay in this browser. ", about);
  const bench = h("section", { class: "bench" }, ...touchNote(), h("div", { class: "mon-col" }, monitor, strip.element), fitWidth(rack.element), note, mirror);
  const drawer_ = drawer(
    {
      settings: { element: settings.element, button: bar.settings, label: "Settings" },
      debugger: { element: dbg.element, button: bar.debug, label: "Debugger", shown: (o) => dbg.setOpen(o) },
    },
    { focusBack: bar.focusBack, phone, behind: [bar.element, bench] },
  );
  settings.header.append(drawer_.closers.settings);
  dbg.head.append(drawer_.closers.debugger);
  root.append(bar.element, h("main", { class: "wrap" }, bench, drawer_.element));
  drawer_.restore(); // the debugger, if it was left open

  let clockLabel = "7.16 MHz";
  let mirrored = "";
  let lastMirror = 0;
  let wasPowered = true;
  port.onFrame((f) => {
    if (f.apple !== apple) {
      apple = f.apple;
      crt.setApple(f.apple);
      strip.signal.textContent = f.apple ? "APPLE IIe" : "VGA 640×480";
    }
    // The machine's video mode changed (or Display's scene did, with Apple II mode): follow it.
    if (follow.machine(machineScreen(f)) || sceneFor(follow.screen) !== scene) applyScreen();
    if (pasting && !f.keysQueued) pasting = false;
    const typing = pasting && f.keysQueued >= 40; // not for a line or two
    if (stopTyping.hidden === typing) stopTyping.hidden = !typing; // set only on a change: no restyle a frame
    appleRoms.update(f);
    for (const d of drives) d.update(f);
    card.update(f);
    speaker.feed(f);
    sound.update();
    tune.update(f);
    strip.update(f.powered);
    if (f.serialOut.length) terminal.write(f.serialOut);
    if (f.video) crt.setVideo(f.video);
    else if (!f.powered && wasPowered) crt.clearVideo(); // the monitor loses its signal
    wasPowered = f.powered;
    crt.setPowered(f.powered);
    bar.update(f);
    rack.update(f);
    dbg.update(f);
    if (f.hz > 0) clockLabel = `${(f.hz / 1e6).toFixed(2)} MHz`;
    strip.status.textContent = statusText(f, clockLabel);
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
    strip.status.textContent = "CRASHED";
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
    .then(async (opened) => {
      bios.connected(opened.bios);
      await appleRoms.restore(); // a power cycle, if a cartridge was kept: before the panel's state goes out
      panel.applyAll();
      port.setLights(true);
      sound.restore(); // left on last time: it plays from the first click or key
      dbg.connect();
      glass.focus();
      await Promise.all(drives.map((d) => d.restore())); // the disks kept from last time, which stay in through power cycles: last, as IndexedDB can be slow to answer
    })
    .catch((err: unknown) => {
      strip.status.textContent = "NO MACHINE";
      const why = String(err instanceof Error ? err.message : err);
      // A BIOS of the owner's that won't open: say how to get the page's own back.
      const mine = bios.mine ? `\rYour BIOS, ${bios.mine.name}, did not start. Settings, ROMs: Use built-in.\r` : "";
      terminal.write(new TextEncoder().encode(`\r${why}\r${mine}`));
      crt.draw(performance.now());
      mirror.textContent = terminal.text();
    });
}

start(document.getElementById("app")!, new WasmPort({ bios: keptBios()?.bytes }));
