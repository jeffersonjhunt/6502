; OOUI.BNK's interface (hw/ooui.md, "U3a: OOUI as a banked library"): the
; calls, in the stubs' and the file's entry table's order, and the imports,
; in the import table's order (FARCALL first: ui_load's, from function 72).
; Both are only ever added to, at the end, and keep their meanings: the
; interface version (UI_IFACE, ooui.inc) goes up with each addition.
; tools/mkbank.py reads the UIENT, UIIMP and UIIMPV lines (UIIMPV: a
; variable, which no JSR or JMP may name: rules.py's rule 4, the U3a
; review's C6); the program part's uistub.s and uiload.s expand the macros.

.macro UI_ENTRIES
        UIENT   ui_open
        UIENT   ui_close
        UIENT   ui_poll
        UIENT   ui_show
        UIENT   ui_hide
        UIENT   ui_flags
        UIENT   ui_settext
        UIENT   ui_setvalue
        UIENT   ui_getvalue
        UIENT   ui_dirty
        UIENT   ui_redraw
        UIENT   ui_focus
        UIENT   ui_getfocus
        UIENT   ui_rect
        UIENT   ui_info
        UIENT   ui_theme
        UIENT   ui_suspend
        UIENT   ui_resume
        UIENT   ui_setrange
        UIENT   ui_msgbox
        UIENT   ui_pick
        UIENT   ui_relist
.endmacro

.macro UI_IMPORTS
        UIIMP   FARCALL
        UIIMP   GX_VALLOC
        UIIMP   GX_VFREE
        UIIMP   GX_VMAP
        UIIMP   GX_VCOPY
        UIIMP   GX_VFILL
        UIIMP   GX_LAYER
        UIIMP   GX_PALOFS
        UIIMP   GX_SHOW
        UIIMP   GX_SPRIMG
        UIIMP   GX_SPRATTR
        UIIMP   GX_SPRPOS
        UIIMP   GX_SPRSHOW
        UIIMP   GX_PALSET
        UIIMP   GX_PALSHOW
        UIIMP   GX_CONSOLE
        UIIMPV  GX_V
        UIIMPV  GX_R
        UIIMPV  GX_X
        UIIMPV  GX_Y
        UIIMPV  GX_A
        UIIMPV  GX_C
.endmacro
