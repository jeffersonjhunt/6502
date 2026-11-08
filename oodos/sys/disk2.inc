; The Disk II card's switches in native mode: backplane.inc's DISK2_x (the
; Technical Reference's entries, chapter 10), which disk2.s and common.s's
; NMI use by name. What follows is the rule for touching them.
;
; A read of an even one gives the data register, of an odd one $FF; any
; access works the switch, a write puts its byte on the card's bus. Only
; plain absolute loads and stores may touch them, or an absolute,X load
; that stays inside the card's page (the W65C02S makes it once, no read
; before it): an indexed access across a page reads another address
; first, and a read-modify-write touches it twice.
