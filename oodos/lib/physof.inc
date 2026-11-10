; OOLIB's PHYSOF (lib/physof.s): the physical address of a CPU address, for a
; module that hands one to function 82 or 83 (sid.s, mu.s).

.ifndef PHYSOF_INC
PHYSOF_INC = 1

; PHYSOF: A/X a CPU address (the program's windows: function 73): PHYS, 3
; bytes, its physical address. A, X and Y changed.
.global PHYSOF, PHYS

.endif
