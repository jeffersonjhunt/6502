; The Disk II card's switches in native mode, as offsets from DISK2. The
; addresses are backplane.inc's (DISK2_x, the Technical Reference's
; entries, chapter 10), which the driver writes by name; these offsets are
; taken from them, so the values are said once, for common.s's NMI, which
; still writes DISK2 + D2_x.
;
; A read of an even one gives the data register, of an odd one $FF; any
; access works the switch, a write puts its byte on the card's bus. Only
; plain absolute loads and stores may touch them, or an absolute,X load
; that stays inside the card's page (the W65C02S makes it once, no read
; before it): an indexed access across a page reads another address
; first, and a read-modify-write touches it twice.

D2_PH0OFF       = DISK2_PH0OFF - DISK2 ; stepper phase 0 off, then on; 1-3 the same
D2_PH0ON        = DISK2_PH0ON - DISK2
D2_PH1OFF       = DISK2_PH1OFF - DISK2
D2_PH1ON        = DISK2_PH1ON - DISK2
D2_PH2OFF       = DISK2_PH2OFF - DISK2
D2_PH2ON        = DISK2_PH2ON - DISK2
D2_PH3OFF       = DISK2_PH3OFF - DISK2
D2_PH3ON        = DISK2_PH3ON - DISK2
D2_MOTOROFF     = DISK2_MOTOROFF - DISK2 ; off: the card keeps it turning a second more
D2_MOTORON      = DISK2_MOTORON - DISK2
D2_DRIVE1       = DISK2_DRIVE1 - DISK2
D2_DRIVE2       = DISK2_DRIVE2 - DISK2
D2_Q6OFF        = DISK2_Q6OFF - DISK2  ; read: the data register; write mode: shift
D2_Q6ON         = DISK2_Q6ON - DISK2   ; sense write-protect; write mode: load
D2_Q7OFF        = DISK2_Q7OFF - DISK2  ; read mode
D2_Q7ON         = DISK2_Q7ON - DISK2   ; write mode
