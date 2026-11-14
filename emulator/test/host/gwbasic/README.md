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
- for a typed session, `NAME.typ` (7b step c: the lines typed at the prompt, each ended by a CR, a program's lines
  and commands alike, with no `NAME.bas`), as `pcbasic -n` with no program, `NAME.typ` on its standard input and
  `NAME.out` its standard output: the whole transcript, from the first Ok, each line as typed and what it printed.

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


The programs of the translation-fidelity review (T1, T2), typed and RUN the same way, each against PC-BASIC's
output (it agrees with the 8086 source on both):

| Program | What |
|---|---|
| `gc1` | strings left as garbage until the space is nearly full, then a DIM: the collection before the array is made |
| `gc2` | the same, then a new simple variable with an array above it: the collection before the arrays move |
| `input2` | INPUT's data ending at a `:` (`5:6`, `"ab":x`): `?Redo from start` |

T3-T5 (LLIST before `'`, LPT1's columns and its CR LF) go to LST:, which PC-BASIC's run doesn't show:
test_gwbasic_core checks them by hand, from GIOLPT.ASM's logic.

## 7b

The programs of 7b's steps (test_gwbasic_stmts), typed and RUN the same way. Step a1's, each against PC-BASIC's
output (it agrees with the 1983 source on all of them):

| Program | What |
|---|---|
| `ongoto` | ON ... GOTO and ON ... GOSUB: index 0 and past the last fall through, 1 to the last, a non-integer index rounded, RETURN to the statement after, spaces in the list, a list ending in a comma, index 255 |
| `on_fcn`, `on_fcb`, `on_ul`, `on_sn`, `on_sn2` | ON's errors: a negative index, one over 255, a missing line, the index at a trailing comma, neither GOTO nor GOSUB |
| `trapres`, `trapnext`, `trapresn` | ON ERROR GOTO with RESUME (an error in PRINT among them), RESUME NEXT (mid-line, after ERROR, after a line's first statement, in IF's THEN), RESUME line, RESUME 0, ON ERROR GOTO in the handler, an unknown code; ERR and ERL |
| `trapinh`, `trapoff`, `trapoff2` | an error in the handler (printed); ON ERROR GOTO 0 in the handler (the error printed) and out of it |
| `trapdeep` | RESUME NEXT three GOSUBs deep inside FOR loops, then the loops and RETURNs |
| `traperl` | ERR and ERL before any error and after RESUME; END in the handler |
| `errkn`, `errun`, `err0`, `err256`, `resnoerr`, `noresume`, `trapul` | ERROR n (known, unknown, 0, 256), RESUME without error, No RESUME, ON ERROR GOTO a missing line |

Errors in direct mode with ON ERROR set can't come from PC-BASIC's run of a file: test_gwbasic_stmts checks them
by hand, from the source (GWMAIN.ASM's NTMDCN).

Step a2's, typed and RUN the same way:

| Program | What |
|---|---|
| `swap` | SWAP of each type, array elements with simple variables, a variable with itself, formula subscripts, a first variable SWAP makes, long names |
| `swaptm`, `swaptm2`, `swapfc`, `swapsn` | SWAP's errors: two types (Type mismatch), a second variable SWAP makes (Illegal function call), no comma |
| `erase`, `gcerase` | ERASE of arrays of each type, first, middle, last, two at once; DIM again; string arrays erased and the string space collected after, the kept strings checked |
| `erasefc`, `erasefc2`, `erasesn` | ERASE's errors: an array not there, a simple variable, a subscript |
| `clear`, `clearrg`, `clearnf` | CLEAR alone, with x, with m; what it drops (variables, arrays, ON ERROR, GOSUB, FOR) |
| `clearcm`, `clearcm2`, `clearom`, `clearom2`, `clearom3`, `clearfc`, `clearfc2`, `clearfc3`, `clearfc4`, `clearsn` | CLEAR's commas and errors: m 240 or less, more than n, n under the variables (Out of memory); n or m 0, x negative, n past the TPA's top (Illegal function call); too much |
| `option`, `opt01`, `optbs`, `optdd`, `optdd2`, `optsn`, `optsn2`, `optsn3` | OPTION BASE 1 and 0, again, after CLEAR; Duplicate Definition; Syntax error |
| `random`, `randomov`, `randomtm`, `randin` (`randin.in`) | RANDOMIZE n (each kind of seed), its errors; RANDOMIZE alone with the seed typed, a bad line asked for again |
| `fout7788` | the owner's G5: .7788379, which the 1983 source's $FOUT prints as .778838 (two roundings in $FOTNV); shipped GW-BASIC prints .7788379 (.differ) |
| `write`, `writemo`, `writesn` | WRITE: numbers, strings, `,` and `;`, alone; Missing operand; Syntax error |
| `strng`, `strfc`, `strfc2`, `strfc3`, `strfc4`, `strtm` | STRING$ of a code and of a string, 0 and 255 long; its errors |
| `lhsmid`, `lhsfc0`, `lhsfc`, `lhsfc2`, `lhstm`, `lhstm2`, `lhssn`, `lhssn2` | MID$ as a statement: each length case, a literal (and DATA) copied first; n 0, past the end; Type mismatch; Syntax error |
| `varptr`, `varpfc` | VARPTR$'s type byte for each type, its address bytes against VARPTR; an undefined variable |

Where PC-BASIC doesn't do what the 1983 source does, `NAME.differ` has the source's output and says why: `clearcm`,
`clearcm2` (no Syntax error for a comma with nothing after it); `clearom`, `clearom2`, `clearom3` (CLEAR's stack
and room checks, which PC-BASIC doesn't make); `opt01` (OPTION BASE 0 when the base is 0 isn't counted); `random`
(the seed is FRCINT's integer, not the number's bytes; and $FOUT's .778838); `randomov`, `randomtm` (FRCINT's
Overflow and Type mismatch); `writesn` (the comma goes out after the separator is checked); `strfc4` (a null
string is Illegal function call). `clearfc4` is the OO-1's own: n past the TPA's top.

By hand in test_gwbasic_stmts (not from PC-BASIC's run of a file): WRITE # (Advanced Feature until 7c), RANDOMIZE
TIMER (TIMER is a variable until 7f: the 1983 source's crunch table has no TIMER), and CLEAR's n and m on the OO-1's
memory map (FRE(0) by n; the GOSUB depth by m).

Step b's, typed and RUN the same way:

| Program | What |
|---|---|
| `fn` | DEF FN and FN: each type, the result made the function's type and each argument its parameter's, 0 to 3 parameters, a parameter named as a variable (kept), nesting, a function as another's argument, in FOR and GOSUB, DEF FN again |
| `fnshad` | parameters named as variables that are set after the DEF, string and number |
| `fngc` | the string space (about 900 bytes, the rest taken by an array) collected while string functions' parameters are bound, in PARM1 and in the outer calls' blocks; each result checked |
| `fnproj` | a function whose result is its string parameter (copied before PARM1 is put back) |
| `fnscope`, `fndup` | a function called from another sees the variables, not its caller's parameters; two parameters of one name |
| `fnuf`, `fntm`, `fntm2`, `fntm3`, `fnom`, `fnfc` | FN's errors: Undefined user function, Type mismatch (the result; an argument of either kind), recursion to Out of memory, more than PRMSIZ's bytes of parameters (Illegal function call) |
| `fnsn`, `fnsn2`, `fnsn3`, `fnsn4`, `fnsn5` | Syntax error: too few or too many arguments, no "(", a body that doesn't end the DEF's statement, a parameter list without its comma |

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`): `fnscope` (PC-BASIC's body sees its caller's
parameters), `fndup` (the source finds the second X), `fnsn4` (PC-BASIC takes the body to the ")"), `fnfc`
(PC-BASIC has no PRMSIZ), `fnproj` (PC-BASIC gives the variable of the parameter's name).

By hand in test_gwbasic_stmts: DEF FN in direct mode (Illegal direct), a function called from a typed line after
RUN, DEF SEG (Advanced Feature until 7c), and USR (PC-BASIC's runs 8086 code): 6502 routines READ into an integer
array, at VARPTR's address.

Step c's are typed sessions (`NAME.typ`, above): test_gwbasic_stmts starts GW-BASIC and types each line when it
waits for one, and the transcript must be PC-BASIC's (it agrees with the 1983 source on all three):

| Session | What |
|---|---|
| `renum` | RENUM of a program with every kind of line reference CRUNCH makes a line number (GOTO, GOSUB, THEN, ELSE, ON ... GOTO and GOSUB lists, RESTORE, RESUME, RETURN, RUN, LIST, LLIST, DELETE, EDIT, AUTO, RENUM, `ERL=` and `ERL<>`; not `110=ERL`), `ON ERROR GOTO 0` kept, a missing line ("Undefined line 999 in 160"); RUN, then `RENUM 1000,30`, `RENUM`, `RENUM 100,,5`, each LISTed and RUN |
| `renumerl` | RENUM of an ON ERROR GOTO handler with `IF ERL=40 THEN RESUME 50` |
| `renumfc` | RENUM's errors: a new number at or below a line kept, an increment of 0, past 65529 or 65535 (Illegal function call, nothing changed); too much, a letter (Syntax error); the last allowed (65529); `.` |

PC-BASIC's transcript of AUTO and EDIT is its own interface's, not GW-BASIC's screen (AUTO's number is shown twice,
EDIT's line is typed in as input), so test_gwbasic_stmts checks them by hand, from the source: AUTO's numbers, "*"
for a line that exists, an empty line, ^C, the end past 65529, NEW, the old increment, `.`, and the errors; EDIT n,
`.`, a missing line, a number with more after it, and EDIT in a program. On OODOS the number AUTO offers and the
line EDIT lists are function 10's preload (as a Syntax error's line in 7a): shown at once and edited as typed.

Step d's, typed and RUN as 7a's, each line labelled:

| Program | What |
|---|---|
| `pustr` | the string fields: `!`, `&`, `\ \` of 2 to 6, strings shorter and longer than the field, null strings; text before, between and after; `_` escapes; `\` that is no field; the format reused for a longer list; `;` and `,` between values and at the end; a `+` before a string field; a format in a variable |
| `puint` | integers in each numeric form: `#`, leading `+`, trailing `+` and `-`, `**`, `$$`, `**$`, `,`, a point, `^^^^`; positive, negative, zero, -32768 and 32767; `%` when too wide |
| `pusng` | singles: rounding at the field's last digit (and into a digit more), places after the point more than the digits, numbers under 1, 10^16 and more (free format after the `%`), commas with zeros from the exponent, `**$` with commas, each sign form |
| `puexp` | E format: places before the point or none, a sign before or after, rounding into a digit more, zero, the largest and smallest, `^^^` (no field) and `^^^^^` (one more `^`), with `**`, `$$` and `,` |
| `pudbl` | doubles: 16 digits, rounding, 10^16 and more, E format with a `D` |
| `pumisc` | fields left over after the list, values left over, `+`, `.`, `$` and `*` that are no field's, 23 and 24 digits, a format built by an expression, -0 |
| `pugc` | the format, a temporary in the string space, moved by collections while its list is evaluated (a string space of about 700 bytes): CRDNUS finds its characters again from the descriptor |
| `pulong` | the longest field (two places, a comma, 22 after the point, a trailing sign) of 1D15: `%`, then the program LISTed (FBUFF's end) |
| `pufc0`-`pufc7` | Illegal function call: a null format, a format with no field (printed first) with values and without, 25 digits; and a missing value (Missing operand) |
| `putm`, `putm2`, `putm3` | Type mismatch: a number for `!`, a string for `##`, a number for the format |
| `pusn`, `pusn2`, `pusn3` | Syntax error: no delimiter between values, `,` for the format's `;`, two strings run together |

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`), each checked against a model of MATH1.ASM's and
MATH2.ASM's PRINT USING routines on `mbf.py`'s arithmetic: `pusng` and `pudbl` (10^16 and more is `$FOUT`'s free
format after the `%`, the sign lost; `$PUFXE` drops a leading `0` only when the field is too wide; -.0005# rounds to
-0.001), `puexp` and `pudbl` (`$PUFE` puts the exponent up one after a rounding into a digit more; a negative number
whose point is at FBUFF+2 loses its sign), `pumisc` (-0 is the integer 0), `pufc2` (the text before the field is
printed before the missing value is found), `pufc3` (the source counts the point among the 24), `pufc4` and `pufc6`
(a format with no field and no values is Illegal function call, after it is printed).

By hand in test_gwbasic_stmts: LPRINT USING to LST: (PC-BASIC's printer isn't the oracle's).


Step e's, typed and RUN as 7a's:

| Program | What |
|---|---|
| `onev` | ON event GOSUB with no event raised (none is taken): ON KEY(1) and (14), with spaces; ON COM(1) and (2); ON PEN; ON STRIG(0), (2), (4), (6); GOSUB 0; a rounded n; ON's numeric form right after them |
| `ontimer`, `onplay` | ON TIMER(n) GOSUB (TIMER is no reserved word in the 1983 source: an array element, 0, so ON's numeric form goes on) and ON PLAY(n) GOSUB (PLAY a statement's token: Syntax error) |
| `onevnl` | ON KEY(n) GOSUB with no line (LINGET's 0: the trap's line cleared) |
| `onevfc0`-`onevfc7` | Illegal function call: KEY(0), KEY(15), KEY(256), COM(3), COM(0), STRIG(1), STRIG(8), KEY(-1) |
| `onevul` | a missing line: Undefined line number |
| `onevsn`-`onevsn6` | Syntax error: two lines, GOTO, no `(`, no GOSUB, no `)`, something after the line |

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`): `onevnl` (the source takes no line as 0;
PC-BASIC gives Syntax error), `onevfc1` (the source has 14 key traps, OEM.H's NMKEYT; PC-BASIC takes 15-20), `onplay`
(the source has no ON PLAY).

By hand in test_gwbasic_stmts (PC-BASIC can't raise a trap from outside a program): the traps raised through the
test-only trigger (TRPTST, `p3/trptst.s`), each case's output written from the source's routines; and the trap
test's cycles at NEWSTT.
