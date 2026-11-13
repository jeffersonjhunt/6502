# 9 User interface (OOUI)

**A placeholder.** This chapter is written in OOUI's phase U5. Until then the API is
[hw/ooui.md's](../../hw/ooui.md): its phase U0 set it down and the owner accepted it, with the six recommended
answers to its open questions, on 2026-11-11, and the later phases built it, so this chapter doesn't repeat it.

**What is built so far** (since OODOS 0.9; [hw/ooui.md](../../hw/ooui.md), phases U1 to U3b): the core and the text
backend (the desktop, windows, panels, labels, buttons, check boxes, the menu bar and its menus, the focus, two
themes, the pointer); in U2 the group and radio buttons, a choice, scroll bars, list boxes, a text field, bars,
sliders and the canvas, with the message box (`ui_msgbox`) and the file picker (`ui_pick`), which reads the
directory of an SD drive, M: or an Apple CP/M disk image a few entries a frame. Since U3a OOUI is a banked library,
`OOUI.BNK` (on A:): a program calls `ui_load` to load it into three blocks of its own and reaches it through small
stubs, about 3 KB of the program's memory with its state
([hw/ooui.md, "U3a: OOUI as a banked library"](../../hw/ooui.md#u3a-ooui-as-a-banked-library)). U3b added what
OO-Amp's port asked for (own thumbs, hit margins, the picker's Add all) and made the repaint and the hit tests
cheaper ([hw/ooui.md, "U3b: the outcome"](../../hw/ooui.md#u3b-the-outcome)). The reference comes with phase U5.

**A program to read:** `UIDEMO.COM`, on the demo card's A:, is an OOUI program a visitor runs in under a
minute: one window with a menu bar, check boxes, radio buttons, a slider and a bar that follows it, a list, a text
field, the message box and the file picker, both themes, by the keyboard and the mouse. Its source,
[oodos/demo/uidemo.s](../../oodos/demo/uidemo.s), is a whole OOUI program in about 500 lines: the event area and
OOLIB set up, `ui_load`, the table written with the layout macros, the `ui_poll` loop, and `ui_close` before the
warm boot. It links as OOUI's test programs do, with `ooui.lib` and `oolib.lib` and a config that puts `UISTATE`
at `$8000` ([oodos/demo/uidemo.cfg](../../oodos/demo/uidemo.cfg)):
`make -C oodos SRC=demo OOUI_USERS=uidemo OOUI_CFG=demo/uidemo.cfg build/uidemo.com`. It is 5,202 bytes.

OOUI is a library of 6502 code that a program loads and calls, as it calls OOLIB's graphics library, to get the same
windows, menus, dialogs, lists, scroll bars and file picker as every other OO-1 program. The program describes its
widgets in a table in its own memory; OOUI draws them on the video card's layers, takes the keyboard and the mouse
from OOLIB's event area, and hands the program back events. The program never draws a widget itself.

What U0 settled, in short ([hw/ooui.md, "Open Questions"](../../hw/ooui.md#open-questions)):

- **The name** is OOUI, its calls `ui`.
- **It is linked into each program,** as OOLIB is; a shared banked block is weighed again once OO-Amp runs on it.
  (U3a since made it that banked block, `OOUI.BNK`.)
- **Its default theme** is an OO-1 house style, with a second theme beside it to show that a theme restyles
  everything.
- **The text backend comes first:** 80×30 cells on the card's two layers. Whether a bitmap backend follows, and a
  drawing assist with it, is decided after OO-Amp is rebuilt on OOUI. A program's own pixels, such as OO-Amp's
  visualizer, go on a canvas widget.

OOUI takes its card memory from OOLIB, under OODOS's interface version 8 on video card revision 2 ([chapter 8,
"Video memory"](08-game-services.md#video-memory)), and leaves the console's card memory and Apple II mode alone.
