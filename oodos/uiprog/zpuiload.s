; OOUI's zero page in the program (hw/ooui.md, U3a): ui/zpuicore.s's, the
; program part's zero page module, right after uiload (libzp.py's rule), so the
; program's link places it and OOUI.BNK's fixups follow.
        .include "zpuicore.s"
