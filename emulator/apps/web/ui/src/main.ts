/**
 * The OO-1 workbench page (mockup C): the toolbar, the CRT monitor with the
 * serial terminal on it, the front panel in its rack, and the debugger
 * pane beside them. Everything talks to the machine through one MachinePort.
 *
 * The monitor's scenes: Text (the serial terminal), VGA (the video card)
 * and Apple II. Query options: ?demo=scenes shows the mockup's picture on
 * the Apple II scene instead of NO SIGNAL.
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
import { keyBytes, pasteBytes } from "./crt/keyboard";
import { UsbKeyboard } from "./crt/usb-keyboard";
import { Terminal } from "./crt/terminal";
import type { FrameState, MachinePort } from "./port/machine-port";
import { WasmPort } from "./port/wasm-port";
import { h, prefs, segmented } from "./ui/dom";
import { debuggerPane } from "./debugger/pane";
import { frontPanel } from "./panel/panel";
import { PanelState } from "./ui/panel-state";
import { toolbar } from "./ui/toolbar";

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
  const params = new URLSearchParams(location.search);
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
  const crt = new Crt(canvas, terminal, { demo: params.get("demo") === "scenes", reducedMotion });
  let caps = true;

  const dbg = debuggerPane(port);
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
      "aria-label": "Terminal: click and type",
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
  // card's on VGA: a VGA monitor and a USB keyboard are a whole computer.
  let scene: Scene = "text";
  const usbKeys = new UsbKeyboard((usage, down) => port.sendKey(usage, down));
  const onUsb = () => scene === "vga" && port.caps.usb;
  glass.addEventListener("keydown", (e) => {
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
    if (onUsb()) port.sendMouse({ ...vgaPoint(e), buttons: BUTTONS(e.buttons) });
  });
  for (const type of ["mousedown", "mouseup"] as const)
    glass.addEventListener(type, (e) => {
      if (onUsb()) port.sendMouse({ ...vgaPoint(e), buttons: BUTTONS(e.buttons) });
    });
  glass.addEventListener(
    "wheel",
    (e) => {
      if (!onUsb()) return;
      e.preventDefault();
      port.sendMouse({ wheel: Math.sign(e.deltaY) });
    },
    { passive: false },
  );
  glass.addEventListener("contextmenu", (e) => {
    if (onUsb()) e.preventDefault(); // the right button is the machine's
  });
  glass.addEventListener("paste", (e) => {
    e.preventDefault();
    port.sendSerial(pasteBytes(e.clipboardData?.getData("text") ?? "", caps));
  });

  // The video card's frames are copied out only while its scene is showing.
  const showScene = (s: Scene) => {
    if (s !== scene) usbKeys.releaseAll();
    scene = s;
    bar.setCapsEnabled(!onUsb());
    glass.setAttribute("aria-label", onUsb() ? "VGA screen: click and type on the USB keyboard" : "Terminal: click and type");
    crt.setScene(s);
    glass.dataset.scene = s;
    port.watchVideo(s === "vga");
    if (s !== "vga") crt.clearVideo();
    prefs.set("scene", s);
  };
  const savedScene = (["text", "vga", "apple"] as Scene[]).find((s) => s === prefs.get("scene")) ?? "text";
  const scenes = segmented<Scene>(
    "Scene",
    [
      { value: "text", text: "Text", title: "The serial terminal" },
      { value: "vga", text: "VGA", title: "The video card: VGA 640x480, what the BIOS puts on screen" },
      { value: "apple", text: "Apple II", title: "Apple II video modes" },
    ],
    savedScene,
    (s) => {
      showScene(s);
      glass.focus(); // the keyboard is still the serial line's
    },
  );
  showScene(savedScene);
  const status = h("span", { "data-testid": "status" }, "STARTING");
  const monitor = h(
    "div",
    { class: "mon" },
    glass,
    h("div", { class: "mon-foot" }, h("span", {}, h("i", {}), "OO-1 · VGA 640×480 · ", status), scenes.element),
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
    ".",
  );
  const bench = h("section", { class: "bench" }, ...touchNote(), monitor, rack.element, credits, mirror);
  root.append(bar.element, h("main", { class: "wrap" }, bench, dbg.element), dbg.reopen);

  let clockLabel = "7.16 MHz";
  let mirrored = "";
  let lastMirror = 0;
  let wasPowered = true;
  port.onFrame((f) => {
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

  // Keep the canvas's pixels matched to the glass on this display; a move
  // to another display, or zooming, fires resize too.
  const fit = () => crt.resize(canvas.getBoundingClientRect().width, devicePixelRatio || 1);
  new ResizeObserver(fit).observe(canvas);
  window.addEventListener("resize", fit);
  void document.fonts.load("20px VT323").then(() => crt.fontsReady());
  port
    .connect()
    .then(() => {
      panel.applyAll();
      port.setLights(true);
      dbg.connect();
      glass.focus();
    })
    .catch((err: unknown) => {
      status.textContent = "NO MACHINE";
      terminal.write(new TextEncoder().encode(`\r${String(err instanceof Error ? err.message : err)}\r`));
      crt.draw(performance.now());
      mirror.textContent = terminal.text();
    });
}

start(document.getElementById("app")!, new WasmPort());
