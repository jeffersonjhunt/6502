; SIDPLUGZ: SIDPLUG linked without its caller's zero page (test_oodos_playsid):
; the library's zpsid comes with lib-sid, and function 59 refuses it.

        .include "sidplug.s"
