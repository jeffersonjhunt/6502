# GW-BASIC's test programs and the oracle's outputs

The programs test_gwbasic_core types into GW-BASIC on OODOS (hw/oodos.md, "The plan, phase 7": testing), each
`NAME.bas` with `NAME.out`, what it must print. The programs are the project's own.

Each `NAME.out` is PC-BASIC's output for `NAME.bas`, made by `oodos/gwbasic/tools/oracle.sh`:

- PC-BASIC 2.0.7, the wheel `pcbasic-2.0.7-py2.py3-none-any.whl`, sha256
  `586fd172fbe08cb1f4a05c980177abba3e983229c538b9e531f958719a4f90e7`, checked before it is installed;
- in a throwaway container of `python:3.12-slim`, by digest
  `python@sha256:5e2bdb1b0d542d5039cffb8181c65930093949194a65c365dfb1b0fe13a4fa5b`, removed after the run;
- run as `pcbasic -n NAME.bas -q --output=NAME.out`; or, for a program with `NAME.in` (the lines typed at its
  INPUTs, each ended by a CR), as `pcbasic -n NAME.bas -q`, `NAME.in` on its standard input and `NAME.out` its
  standard output, which shows the lines as typed. (PC-BASIC's `--input` option ends the run before the last
  lines are read.)

The program goes in and only its output comes out, by `docker cp`. Nothing of PC-BASIC (GPLv3) is in the repo:
only these outputs. An `.out` written by hand (an interactive transcript PC-BASIC's `-n` run can't give) says so
in a comment of the test that uses it.

Run the oracle again only when a program changes, and look at the difference before committing it.

The programs (step I1 of 7a), each typed line by line at GW-BASIC's prompt and then RUN:

| Program | What |
|---|---|
| `run1` | lines run in order, GOTO, IF, variables, `?` for PRINT |
| `tron` | TRON and TROFF in a program: `[30]` before each line |
| `stop`, `end` | STOP's "Break in 30"; END |
| `errul`, `errtm`, `errsn` | Undefined line number, Type mismatch, Syntax error: "... in 20" |
| `list`, `listr` | a program that LISTs itself (every kind of token and constant), and LIST 20-30 |

The programs of step I2, typed and RUN the same way:

| Program | What |
|---|---|
| `for` | FOR and NEXT: single and integer loops, negative and fractional steps, loops run no times, nesting, `NEXT B,A`, a FOR run again before its NEXT, STEP 0, an integer loop's Overflow |
| `gosub` | GOSUB and RETURN: nesting, `RETURN line`, a GOSUB in a FOR, a RETURN out of a FOR, recursion |
| `while` | WHILE and WEND: nesting, none, inside and around FOR, a WHILE run again before its WEND |
| `print`, `tabneg` | PRINT: `;`, the print zones of `,`, a line's end, TAB and SPC (and of a negative number), numbers of each type |
| `read` | READ, DATA and RESTORE: numbers and strings, quoted or not, colons, `RESTORE line`, DATA anywhere |
| `input` (`input.in`) | INPUT and LINE INPUT: prompts, "? ", several values, quoted strings, a subscript, "?Redo from start" |
| `e_nf`, `e_nf2`, `e_rg`, `e_od`, `e_dsn`, `e_fn`, `e_wh`, `e_we`, `e_ftm`, `e_ftm2`, `e_ov`, `e_litm` | the errors: NEXT without FOR, RETURN without GOSUB, Out of DATA, Syntax error in a DATA, FOR without NEXT, WHILE without WEND, WEND without WHILE, Type mismatch, Overflow |
| `spk_*` | the size spike's seven programs (`oodos/gwbasic/test/check.py --write-programs`) |

Where the 1983 source and PC-BASIC differ, the port keeps the source's behaviour, and `NAME.differ` is what it
must print instead: its first line, `# why`, says why, the rest is the text, line by line (as
`oodos/gwbasic/test/programs/` has it). `NAME.out` stays PC-BASIC's, for the record:

- `for`: a FOR that runs no times leaves its variable at the start, and STEP 0 never ends;
- `tabneg`: TAB and SPC of a negative number are of 0;
- `e_dsn`: READ's Syntax error is in the line of the DATA it was reading;
- `e_fn`: the message is "FOR Without NEXT";
- `spk_rnd1`, `spk_rnd3`: 0 MOD 0 is 0.

Not checked against PC-BASIC: LINE INPUT keeps the blanks typed at a line's end (OODOS's function 10 gives them;
GW-BASIC's screen editor reads the line back from the screen, and PC-BASIC drops them), so `input.in` types none
there; `INPUT;`, whose cursor GW-BASIC keeps on the line (test_gwbasic_core checks 7a's by hand).

PC-BASIC writes `$FF` after an error's message (GW-BASIC's "not typed" mark); the test drops it, as the OO-1's
console doesn't show it.

