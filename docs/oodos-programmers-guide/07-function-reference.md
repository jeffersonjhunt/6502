# 7 Function reference

One entry for each BDOS function, in number order: what goes in, what comes out, the errors, what else changes,
and how it differs from CP/M 3's function of the same number. Every entry has the same headings
([docs/templates/function.md](../templates/function.md)) and an anchor, `bdos-` and the number, so
`07-function-reference.md#bdos-9` is function 9's. [docs/index/functions.json](../index/functions.json) lists every
function number with its anchor once its entry is written.

How to call a function, and how registers and numbers are written here, is in
[chapter 2](02-system-interface.md#calling-the-bdos) and [docs/README.md](../README.md#conventions). In short:
`JSR BDOS` with the function in Y, a byte in A, a word in A (low) and X (high); the result comes back in A, or A
and X. Y and the flags are not kept, and the decimal flag is clear on return.

The entries for the other functions are written in G1. Function 9's is the first, and the model for the rest.

<a id="bdos-9"></a>
### 9 C_WRITESTR: print a string

#### Entry

- Y = 9 (`C_WRITESTR`).
- A = the string's address, low byte; X = its high byte. The address is the program's own, anywhere in
  `$0000`–`$BFFF`.
- The string ends at the output delimiter, `$` (`$24`) unless function 110 has changed it. The delimiter is not
  printed.

#### Exit

- A = 0, X = 0.
- Y and the flags are not kept; the decimal flag is clear.

#### Errors

None.

#### Side effects

Each character goes to the console as function 2 sends it:

- A tab (`$09`) moves to the next column that is a multiple of 8, unless bit 2 of the console mode (function 109)
  asks for raw output.
- While printer echo is on (^P), each character goes to LST: as well, unless the output is raw.
- Before each character the BDOS looks at the keyboard, unless bit 1 of the console mode turns that off. A ^Q
  or ^P on its own is dropped; any other key but ^S is kept, one, for the next console input. ^S stops the
  output: then ^Q goes on, ^P turns printer echo over and stays stopped, ^C ends the program by the warm boot
  (unless bit 3 of the console mode), and any other key rings the bell.
- The SCB's console column follows what was printed.

The CCP sets the delimiter back to `$` and the console mode to 0 at each warm boot, so a program starts with both.

#### Cycles

Not measured. `test_oodos_sys` measures function 2's cost a character, not function 9's.

#### Since

OODOS 0.2

#### Status

**built**

#### CP/M 3

The same, but for the registers: the string's address goes in A and X, where CP/M 3 takes it in DE, and A and X
come back 0.

#### Example

[`docs/examples/writestr.s`](../examples/writestr.s), built and run from the CCP by `test_docs_examples`:

<!-- listing: ../examples/writestr.s -->
```
; WRITESTR: prints a line through BDOS function 9, C_WRITESTR, then ends by
; the warm boot. The Programmer's Guide's example for function 9.

        .include "oodos.inc"

        OODOS_HEADER                    ; once per program

        .code
start:  lda     #<msg                   ; the string's address: low in A,
        ldx     #>msg                   ; high in X
        ldy     #C_WRITESTR             ; the function in Y
        jsr     BDOS
        jmp     WBOOT                   ; back to the CCP

        .rodata
msg:    .byte   "Written by function 9", 13, 10, '$'
```

At the prompt, with WRITESTR.COM on M:, it prints
([`writestr.out`](../examples/writestr.out)):

<!-- listing: ../examples/writestr.out -->
```
Written by function 9
```
