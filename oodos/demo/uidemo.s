; UIDEMO: OOUI shown off, for a visitor on the demo card (make_card.sh).
; One window with a menu bar (File: About, Quit; View: the Turbo Vision
; theme), check boxes, radio buttons, a slider with a bar that follows it,
; a list, a text field and buttons; Open file... puts up the file picker on
; A: and B:, About a message box. The line at the window's foot says what
; the last event did. Quit, the window's close box, or Escape with nothing
; open, closes OOUI (ui_close: the console's screen back) and ends with the
; warm boot, back at A>.
;
; Linked as OOUI's test programs are (demo/uidemo.cfg: UISTATE at $8000),
; with build/ooui.lib and build/oolib.lib; it loads OOUI.BNK by its default
; name, A:OOUI.BNK in user 0 (ui_load). Everything OOUI reads while it draws
; (the table, the texts, the themes, the event area, the picker's buffers)
; is in the image or its BSS, below $4000 (the W1 contract; tools/w1.py).
;
; For test_oodos_ui: each event ui_poll returns is kept in EVLOG, four
; bytes (its code, its widget, UV_VALUE), EVN counting them (a ring of 64:
; the entry is EVN mod 64); READY is 1 once the first paint is done.

        .setcpu "65C02"
        .include "oodos.inc"
        .include "oolib.inc"
        .include "ooui.inc"

        OODOS_HEADER

; HID usages
U_A     = $04
U_O     = $12
U_Q     = $14
U_T     = $17
U_ESC   = $29
U_F1    = $3A

VOL0    = 40                            ; the slider's value at the start
NFILES  = 128                           ; the picker's listing: 128 files, 2 KB

        .zeropage
sp_:    .res    2                       ; a string being copied
dp_:    .res    1                       ; where in stat
lead:   .res    1                       ; decimal's: a digit written
div:    .res    1                       ;   the power of ten

        .code
start:  lda     #<oblk                  ; the event area (layout 2), for OOLIB and OOUI
        ldx     #>oblk
        ldy     #O_FRAME
        jsr     BDOS
        lda     #<area
        ldx     #>area
        jsr     GX_INIT
        bcs     novid
        jsr     GX_PALINIT
        bcs     novid
        jsr     GX_SPRINIT
        bcs     novid
        lda     #0                      ; A:OOUI.BNK, user 0
        tax
        jsr     ui_load
        bcs     nobnk
        lda     #<oblock
        ldx     #>oblock
        jsr     ui_open
        bcs     novid
        ldy     #H_BAR                  ; the bar where the slider starts
        lda     #VOL0
        ldx     #0
        jsr     ui_setvalue
        lda     #F_NAME                 ; the focus on the field: type a name
        jsr     ui_focus

loop:   jsr     ui_poll
        cmp     #UE_NONE
        bne     @ev
        lda     #1                      ; drawn
        sta     READY
        bra     loop
@ev:    jsr     log
        jsr     event
        bra     loop

novid:  lda     #<m_vid
        ldx     #>m_vid
        bra     say
nobnk:  lda     #<m_bnk
        ldx     #>m_bnk
say:    ldy     #C_WRITESTR
        jsr     BDOS
        jmp     WBOOT

quit:   jsr     ui_close
        jmp     WBOOT

; The event ui_poll returned kept in EVLOG; A, X and Y kept.
log:    pha
        phx
        phy
        lda     EVN
        and     #63
        asl     a
        asl     a
        tax
        lda     UI_EV + UV_CODE
        sta     EVLOG,x
        lda     UI_EV + UV_HANDLE
        sta     EVLOG + 1,x
        lda     UI_EV + UV_VALUE
        sta     EVLOG + 2,x
        lda     UI_EV + UV_VALUE + 1
        sta     EVLOG + 3,x
        inc     EVN
        ply
        plx
        pla
        rts

; What each event does. A the event, X its widget.
event:  cmp     #UE_MENU
        beq     menu
        cmp     #UE_CLICK
        beq     click
        cmp     #UE_CHANGE
        bne     :+
        jmp     change
:       cmp     #UE_SELECT
        bne     :+
        jmp     select
:       cmp     #UE_CLOSE
        beq     close
        cmp     #UE_PICK
        bne     :+
        jmp     picked
:       cmp     #UE_KEY
        bne     @r
        cpy     #U_ESC                  ; Escape that nothing took
        beq     quit
@r:     rts

menu:   cpx     #I_QUIT
        beq     quit
        cpx     #I_ABOUT
        beq     about
        cpx     #I_THEME
        beq     theme
        rts

click:  cpx     #B_QUIT
        beq     quit
        cpx     #B_ABOUT
        beq     about
        cpx     #B_OPEN
        beq     open
        rts

close:  cpx     #W_MAIN                 ; the close box, or Escape
        beq     quit
        lda     #<s_ok                  ; the message box's OK
        ldx     #>s_ok
        jmp     status

about:  lda     #<mbox
        ldx     #>mbox
        jmp     ui_msgbox

open:   lda     #<pick
        ldx     #>pick
        jmp     ui_pick

; View's item: the other theme, its check mark saying which.
theme:  lda     turbo
        eor     #1
        sta     turbo
        ldx     #0
        ldy     #I_THEME
        jsr     ui_setvalue
        lda     turbo
        beq     @house
        lda     #<UI_TURBO
        ldx     #>UI_TURBO
        jsr     ui_theme
        lda     #<s_turbo
        ldx     #>s_turbo
        jmp     status
@house: lda     #0
        tax
        jsr     ui_theme
        lda     #<s_house
        ldx     #>s_house
        jmp     status

; A check box, the group, the slider, the list or the field changed.
change: cpx     #S_VOL
        beq     vol
        lda     UI_EV + UV_FLAGS        ; the rest: only when the change is done
        and     #UV_LIVE
        bne     @r
        cpx     #C_SOUND
        beq     check
        cpx     #C_GRID
        beq     check
        cpx     #G_SPEED
        beq     speed
        cpx     #L_LIST
        beq     planet
        cpx     #F_NAME
        bne     @r
        jmp     name
@r:     rts

; The bar follows the slider, live.
vol:    ldy     #H_BAR
        lda     UI_EV + UV_VALUE
        ldx     UI_EV + UV_VALUE + 1
        jsr     ui_setvalue
        jsr     begin
        lda     #<s_vol
        ldx     #>s_vol
        jsr     add
        lda     UI_EV + UV_VALUE
        jsr     decimal
        jmp     show

check:  jsr     begin
        txa                             ; its record's text, the & left out
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        lda     tab + 8,x
        pha
        lda     tab + 9,x
        tax
        pla
        jsr     add
        lda     #<s_on
        ldx     #>s_on
        ldy     UI_EV + UV_VALUE
        bne     :+
        lda     #<s_off
        ldx     #>s_off
:       jsr     add
        jmp     show

speed:  jsr     begin
        lda     #<s_speed
        ldx     #>s_speed
        jsr     add
        lda     UI_EV + UV_VALUE
        asl     a
        tay
        lda     speeds,y
        ldx     speeds + 1,y
        jsr     add
        jmp     show

planet: lda     #<s_planet
        ldx     #>s_planet
        bra     row
select: lda     #<s_chose
        ldx     #>s_chose
row:    pha
        phx
        jsr     begin
        plx
        pla
        jsr     add
        lda     UI_EV + UV_VALUE + 1    ; $FFFF: none
        bmi     @none
        lda     UI_EV + UV_VALUE
        asl     a
        tay
        lda     rows,y
        ldx     rows + 1,y
        jsr     add
        jmp     show
@none:  lda     #<s_none
        ldx     #>s_none
        jsr     add
        jmp     show

name:   jsr     begin
        lda     #<s_hello
        ldx     #>s_hello
        jsr     add
        lda     #<namebuf
        ldx     #>namebuf
        jsr     add
        jmp     show

; The picker's answer: 0 a file (its drive and name), else none.
picked: lda     UI_EV + UV_VALUE
        bne     @none
        jsr     begin
        lda     #<s_pick
        ldx     #>s_pick
        jsr     add
        ldx     dp_
        lda     result + UP_EDRIVE
        clc
        adc     #'A' - 1
        sta     stat,x
        inx
        lda     #':'
        sta     stat,x
        inx
        ldy     #0
@n:     lda     result + UP_ENAME,y     ; the name, its spaces left out, a dot before the type
        cpy     #8
        bne     @c
        pha
        lda     #'.'
        sta     stat,x
        inx
        pla
@c:     and     #$7F
        cmp     #' '
        beq     @s
        sta     stat,x
        inx
@s:     iny
        cpy     #11
        bne     @n
        stx     dp_
        jmp     show
@none:  lda     #<s_nopick
        ldx     #>s_nopick
        jmp     status

; The status line: a string (status), or built in stat (begin, add,
; decimal, then show).
status: jsr     begin
        jsr     add
show:   ldx     dp_
        stz     stat,x
        ldy     #L_STAT
        lda     #<stat
        ldx     #>stat
        jmp     ui_settext
begin:  stz     dp_
        rts
; A/X a string added at dp_, an & left out; X kept.
add:    sta     sp_
        stx     sp_ + 1
        phx
        ldx     dp_
        ldy     #0
@c:     lda     (sp_),y
        beq     @d
        cmp     #'&'
        beq     @n
        cpx     #STATLEN - 4
        bcs     @d
        sta     stat,x
        inx
@n:     iny
        bra     @c
@d:     stx     dp_
        plx
        rts
; A, 0-255, in decimal at dp_, no leading zeros.
decimal:
        ldx     dp_
        stz     lead
        ldy     #100
        jsr     @dig
        ldy     #10
        jsr     @dig
        ora     #'0'
        sta     stat,x
        inx
        stx     dp_
        rts
@dig:   sty     div
        ldy     #0
@sub:   cmp     div
        bcc     @put
        sbc     div
        iny
        bra     @sub
@put:   pha
        tya
        ora     lead
        beq     @skip
        sta     lead
        tya
        ora     #'0'
        sta     stat,x
        inx
@skip:  pla
        rts

        .rodata
m_vid:  .byte   "UIDEMO needs the video card, revision 2, and OOUI.", 13, 10, "$"
m_bnk:  .byte   "UIDEMO can't load A:OOUI.BNK (OOUI's library).", 13, 10, "$"

oblock: .word   area, tab               ; ui_open's block
        .byte   tab_COUNT
        .word   0, keys
        .byte   0, 1                    ; the widgets on layer 0, menus and dialogs on layer 1
        .byte   0, 1, 0                 ; sprite 0 the pointer, the only one; OOUI's mouse
        .word   tab_WIDGETS
.assert * - oblock = UO_LEN, error, "ui_open's block"

; Ctrl+Q quits, Ctrl+O opens a file, F1 is About.
keys:   .byte   U_Q, UK_CTRL, I_QUIT, UA_PRESS
        .byte   U_O, UK_CTRL, B_OPEN, UA_PRESS
        .byte   U_F1, 0, I_ABOUT, UA_PRESS
        .byte   0

mbox:   .word   t_about, t_atitle
        .byte   UM_OK, 0
t_about: .byte  "OOUI, the OO-1's UI toolkit: windows, menus,", 13
        .byte   "buttons, lists, sliders and a file picker,", 13
        .byte   "drawn on the video card's text layers.", 13
        .byte   "The keyboard or the mouse: Tab, Alt, Enter.", 0
t_atitle: .asciiz "About UIDEMO"

pick:   .word   t_ptitle, 0             ; every file
        .byte   UP_DRIVES, 1, $FF       ; A: and B:, from A:, the current user
        .word   result
        .byte   1, 0
        .word   0, 0, work, NFILES * UP_ELEN
        .byte   0
.assert * - pick = UP_LEN, error, "ui_pick's block"
t_ptitle: .asciiz "Open a file"

s_vol:  .asciiz "Volume: "
s_on:   .asciiz ": on"
s_off:  .asciiz ": off"
s_speed: .asciiz "Speed: "
speeds: .word   r_slow, r_norm, r_fast
s_planet: .asciiz "Planet: "
s_chose: .asciiz "Chosen: "
s_none: .asciiz "none"
s_hello: .asciiz "Hello, "
s_pick: .asciiz "Picked: "
s_nopick: .asciiz "No file picked"
s_ok:   .asciiz "About: OK"
s_turbo: .asciiz "Theme: Turbo Vision"
s_house: .asciiz "Theme: the OO-1's own"

rows:   .word   p0, p1, p2, p3, p4, p5, p6, p7, p8, p9
NROWS   = 10
p0:     .asciiz "Mercury"
p1:     .asciiz "Venus"
p2:     .asciiz "Earth"
p3:     .asciiz "Mars"
p4:     .asciiz "Jupiter"
p5:     .asciiz "Saturn"
p6:     .asciiz "Uranus"
p7:     .asciiz "Neptune"
p8:     .asciiz "Ceres"
p9:     .asciiz "Pluto"

r_slow: .asciiz "S&low"
r_norm: .asciiz "&Normal"
r_fast: .asciiz "&Fast"

; The screen: the window in the middle of 80 x 30, the menu bar above it.
        UI_BEGIN   tab
        UI_DESKTOP W_DESK
        UI_MENUBAR W_BARM, W_DESK
        UI_MENU    M_FILE, W_BARM, "&File"
        UI_ITEM    I_ABOUT, M_FILE, "&About...", U_F1, 0, 0
        UI_SEP     I_SEP, M_FILE
        UI_ITEM    I_QUIT, M_FILE, "&Quit", U_Q, UK_CTRL, 0
        UI_MENU    M_VIEW, W_BARM, "&View"
        UI_ITEM    I_THEME, M_VIEW, "&Turbo Vision theme", 0, 0, 0
        UI_WINDOW  W_MAIN, W_DESK, 4, 3, 72, 24, "OOUI on the OO-1", UF_CLOSE
        UI_LABEL   L_HEAD, W_MAIN, 0, 0, 0, "Tab moves the focus, Alt+letter opens a menu, Esc quits", UF_CENTRE
        UI_PANEL   P_OPTS, W_MAIN, 1, 2, 22, 4, "Options", UF_FRAME
        UI_CHECK   C_SOUND, P_OPTS, 0, 0, 0, "So&und", 1, 0
        UI_CHECK   C_GRID, P_OPTS, 0, 1, 0, "&Grid", 0, 0
        UI_GROUP   G_SPEED, W_MAIN, 25, 2, 20, 5, "Speed", 1, UF_FRAME
        UI_RADIO   R_SLOW, G_SPEED, 0, 0, 0, "S&low", 0
        UI_RADIO   R_NORM, G_SPEED, 0, 1, 0, "&Normal", 0
        UI_RADIO   R_FAST, G_SPEED, 0, 2, 0, "&Fast", 0
        UI_PANEL   P_LIST, W_MAIN, 47, 2, 22, 12, "Planets", UF_FRAME
        UI_LIST    L_LIST, P_LIST, 0, 0, 0, 0, rows, NROWS, UF_SCROLL
        UI_LABEL   L_VOL, W_MAIN, 1, 8, 8, "Volume", 0
        UI_HSLIDER S_VOL, W_MAIN, 10, 8, 34, 100, VOL0, 5, 0, 0
        UI_LABEL   L_LEVEL, W_MAIN, 1, 10, 8, "Level", 0
        UI_HBAR    H_BAR, W_MAIN, 10, 10, 34, 100, 0
        UI_LABEL   L_NAME, W_MAIN, 1, 12, 8, "Name", 0
        UI_FIELD   F_NAME, W_MAIN, 10, 12, 34, namebuf, NAMELEN, 0
        UI_BUTTON  B_OPEN, W_MAIN, 1, 15, 18, "&Open file...", 0
        UI_BUTTON  B_ABOUT, W_MAIN, 21, 15, 12, "A&bout", 0
        UI_BUTTON  B_QUIT, W_MAIN, 35, 15, 10, "&Quit", 0
        UI_LABEL   L_STAT, W_MAIN, 1, -2, -1, stat, 0
        UI_END     tab
.assert C_SOUND < 16 && C_GRID < 16, error, "check's record offsets fit a byte"

        .data
NAMELEN = 24
namebuf: .res   NAMELEN                 ; the field's text, empty
STATLEN = 64
stat:   .byte   "Ready: try the keyboard and the mouse", 0
        .res    STATLEN - 38
turbo:  .byte   0                       ; 1: the Turbo Vision theme
READY:  .byte   0
EVN:    .byte   0
oblk:   .byte   FT_OPEN
        .word   area
        .byte   0                       ; 256 bytes

        .bss
EVLOG:  .res    4 * 64
area:   .res    256                     ; the event area
result: .res    UP_ELEN                 ; the picker's answer: one file
work:   .res    NFILES * UP_ELEN
