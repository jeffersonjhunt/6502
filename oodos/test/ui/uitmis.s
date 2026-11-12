; UITMIS: UITEST (UIVAR 0) linked with UISTATE one byte off a page
; (test/ui/uimis.cfg), its own copy of uistate.s without the link's check,
; so ui_load's own refusal is seen: UX_STATE (test_oodos_ui; U3a, E2).
UI_NOALIGN = 1
        .include "../../uiprog/uistate.s"
UIVAR = 0
        .include "uitest.s"
