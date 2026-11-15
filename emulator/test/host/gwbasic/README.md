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
| `fnnull` | review U1: an undefined string variable's or INKEY$'s empty result (DSEGZ, below the temporaries) passed as an 11th string argument takes no temporary |

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
| `renumu2` | review U2: a line constant followed at once by another constant (`GOTO 30 &H28`): the line is LINGT3's DX, read before CHRGTR goes past the constant; MAKPTR writes at CONTXT, so the source changes the &H28, not the 30 (`.differ`) |

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

## 7c

The programs of 7c's file layer (test_gwbasic_files: block P4, sequential files), typed and RUN as 7a's, each
against PC-BASIC's output. Each writes its own files and reads them back, so it runs alike in PC-BASIC's working
directory and on the OO-1, where GW-BASIC's files go to the CCP's current drive, A:, when no
drive is named.

| Program | What |
|---|---|
| `f_seq` | PRINT # to a file, CLOSE, then LINE INPUT #, INPUT # and EOF reading it back |
| `f_modes` | OPEN's two forms and every sequential mode, either case, FOR OUTPUT, INPUT and APPEND; APPEND at the end, to a new file and to an empty one |
| `f_close`, `f_reset` | CLOSE of one, a list and all, of one not open and of one twice; a closed file used again; RESET |
| `f_print`, `f_write` | PRINT # with `,` `;` TAB SPC and USING; WRITE #'s quoting; what LINE INPUT # and INPUT # read of them |
| `f_input` | INPUT # of numbers and strings, quoted and not, with commas, CRs, LFs and spaces; a quote never closed; Input past end |
| `f_255` | LINE INPUT # of lines of 255, 254 and 260 characters |
| `f_inps` | INPUT$ from a file, with and without `#`, its CR and LF; PRINT #2 of INPUT$(n, #1); past the end |
| `f_ctlz`, `f_eof` | the ^Z end of a text file; EOF before the end and at it |
| `f_long`, `f_locof` | files of more than one record and of several; LOC and LOF |
| `f_clear`, `f_run`, `f_run2` | CLEAR and RUN close every file, its last record written |
| `f_errs`, `f_errs2`, `f_e52`, `f_e53`, `f_e54`, `f_e55`, `f_e62`, `f_e64` | the errors, trapped (ERR, ERL), then each's message |

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`): `f_write` (INPUT # reads a double as a double:
the source's FINDBL), `f_input` (an LF in an unquoted item is dropped and the item goes on: LPISLF), `f_inps`
(INPUT$ takes the CR and LF before the end as bytes), `f_clear` (CLEAR closes every file), `f_errs` (EOF of an output
file with nothing in its buffer is -1; INPUT$(0, #f) of an output file is Bad file mode; LEN= on a sequential OPEN
is Syntax error), `f_errs2` (a name with only a type is Bad file name). Where the OO-1 differs: `f_locof` (CP/M's
files are whole 128-byte records, so LOF is the records times 128, as on the MS-DOS 1 FCB files the source wrote).

By hand in test_gwbasic_files (PC-BASIC's run of a file can't type at the prompt, nor has the OO-1's drives): END,
NEW and SYSTEM close every file (each read back, the last after SYSTEM by the CCP's TYPE); `A:` named is the current
drive; `Q:` and a device name (`LPT1:`) are Bad file name; a random OPEN is Advanced Feature until X1; INPUT$ from
the keyboard; every record's DMA address in P4's own RAM with block P4 in W3 (V1); and PRINT # to a file timed, its
bytes going to P4 as runs.

7c's LOAD, SAVE and MERGE are typed sessions (`NAME.typ`, as 7b's step c): test_gwbasic_files types each line when
GW-BASIC waits for one, and the transcript must be PC-BASIC's (`NAME.out`), or `NAME.differ`'s. Each writes its own
files and reads them back.

| Session | What |
|---|---|
| `l_save` | a program SAVEd tokenised and LOADed, LISTed, RUN; SAVE ,A and LOAD of the text; a name with no type given `.BAS`; LOAD of a missing file; LOAD ,R, RUN name and RUN name,R; the options' Syntax errors |
| `l_merge` | MERGE of a text file into a program (lines replaced and added); of a tokenised file; of a missing one; with more after the name |
| `l_files` | a file open across LOAD ,R (kept), LOAD and RUN name (closed: Bad file number) |
| `l_text` | text files written by PRINT #: a direct statement in one (Direct statement in file), the ^Z end, a leading LF, a last line with no CR, an empty file |
| `l_prot` | SAVE ,P and LOAD: it runs; LIST, EDIT, a line typed, SAVE and SAVE ,A refused; NEW |

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`): `l_merge` (MERGE of a tokenised file is Bad file
mode, OKGET2's; more after MERGE's name is Syntax error), `l_text` (RUN name of a text file runs at its end at once,
so a last line with no CR isn't entered: NOTCHN), `l_prot` (by hand: the source refuses LIST, EDIT, line entry and
SAVE of a protected program, PROCHK; PC-BASIC lists and edits it).

By hand in test_gwbasic_files: W2, a tokenised LOAD at the workspace's top (CLEAR ,n puts MEMSIZ 90 bytes past the
program's end, sentinels past it unchanged, DM_LOAD used, the program run; 30 bytes lower, Out of memory with nothing
written past it); and a program named on the command line (`GWBASIC PROG`, a name ending at `/`, a missing one).

7c's KILL, NAME, FILES and the directory hook, typed and RUN as the file layer's, each writing its own files:

| Program | What |
|---|---|
| `d_kill` | KILL of a file, then OPEN for input (File not found); of a missing file, an open one, a null name, a number; `?` and `*` |
| `d_name`, `d_name2` | NAME, the file read by its new name; a missing one, to a name there already, an open one, null names, numbers; no AS |
| `d_files` | FILES with `*` and `?`, lower case, a name alone, none matching, one of more than 8 characters; six a line, fewer after PRINT ; |
| `d_dir` | CHDIR to `\`, `.`, `""` and another, MKDIR, RMDIR (the directory hook); a number for the path |
| `d_e58`, `d_e74`, `d_e75`, `d_e76`, `d_e77` | the messages of 58 and 74, and of 75 and 76 (added); 77, past the table |

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`): `d_files` (the source prints no heading and no
Bytes free line, one blank between names and six a line; its OPEN runs a long name into the type), `d_name` (a null
name is Bad file name: NAMFIL), `d_e77` (past the table: Unprintable error). Where the OO-1 differs: `d_dir` (OODOS has
no directories: CHDIR `""` is the root, MKDIR is Path/File access error; the owner's, 2026-11-13).

By hand in test_gwbasic_files: FILES with no name, `"A:"`, `"*.*"` and `"a:*.*"` alike, on the rig's A:; a drive
before a pattern; Rename across disks; and the OO-1's Bad file name for a `?` in NAME's names and for a name function
152 refuses; CHDIR, MKDIR and RMDIR crunched to `$FE` and their tokens (the program's text), LISTed back, and typed.

7c's random files (X1), typed and RUN as the file layer's, each writing its own files:

| Program | What |
|---|---|
| `r_x1` | X1, the plan's test: LEN 100, records 1-5 PUT to a new file, then each GET back as it was written |
| `r_x1b` | records never written, past the end and in the last 128-byte record's tail: zeros, EOF true |
| `r_lens` | LEN 1 (300 records), 128 and the default; FIELD, LSET and RSET; PUT and GET with and without a number; a record never written; LOC |
| `r_order` | records of LEN 100 PUT out of order, one twice, each GET back; GET and PUT of the next record |
| `r_cv` | MKI$, MKS$, MKD$ and CVI, CVS, CVD round trips, their bytes, edge values; a short string, Overflow, Type mismatch |
| `r_field` | FIELD's errors; GET and PUT of a sequential file; a FIELDed string assigned by LET, then LSET; Bad record number; PRINT #, INPUT # and WRITE # in the FIELD buffer, PRINT # past it |
| `r_seq` | one file used as sequential and random in turn |
| `r_sw` | LEN past /S: (128 while the switches aren't read) and LEN 0: Illegal function call |
| `r_move` | strings, a string array and FIELD buffers kept right as random files open and close; a closed file's FIELDed strings |
| `r_eof`, `r_lof` | EOF and LOF of a random file |
| `r_forr` | OPEN ... FOR RANDOM |

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`): `r_field` (FIELD's total past the record is FIELD
overflow; record 32768 is Bad record number; the FIELD buffer keeps what was in it; WRITE # pads the record with
spaces), `r_move` (a closed file's FIELDed strings are null), `r_eof` (EOF is true after OPEN and PUT, until a GET
reads a record that is there), `r_forr` (FOR RANDOM is Syntax error: a random file is OPEN's form with no FOR). Where
the OO-1 differs: `r_lof` (LOF in whole records, as `f_locof`).

By hand in test_gwbasic_files: records of LEN 200 and 1,024 with /S: set to 1,024 (MAXREC, as `/S:1024` would set
it): out of order, across the 128-byte records they span; a record never written; a record's place past 2^23 bytes
(Illegal function call) and the last before it; LEN past /S:. And the FIELD buffers in the TPA: FRE(0) and MEMSIZ
lowered by LEN at OPEN, the buffer's bytes from MEMSIZ + 2, both back at CLOSE.

7c's CHAIN and COMMON, typed and RUN as the file layer's; each writes the program it chains to as text by PRINT #
(its quotes as `CHR$(34)`: GW-BASIC's strings have no doubled quote):

| Program | What |
|---|---|
| `c_common` | COMMON of an integer, a single, a double, a string whose text is in the program, number and string arrays, a long name, `M%( )`, over two COMMONs; the rest gone; an array not kept made again |
| `c_all` | `CHAIN name,,ALL`: every variable kept, a string array too |
| `c_line` | CHAIN to line 30; a chained program chaining itself to line 20 (an expression) with ALL |
| `c_ul` | a CHAIN line that isn't in the program |
| `c_merge` | CHAIN MERGE with a line, ALL and DELETE: overlays in turn, the main program's lines and a GOSUB to them kept, DEFINT kept, a file open across both |
| `c_merge2` | CHAIN MERGE with a line and no ALL: COMMON's variable alone kept, lines replaced |
| `c_types` | CHAIN: the DEF types back to single (a COMMON integer then not found by its letter), OPTION BASE 1 kept |
| `c_files` | a file open for output across a CHAIN, written by the chained program, read back |
| `c_miss` | CHAIN of a file that isn't there, trapped, the variables kept |
| `c_e2`-`c_e7` | the errors: `COMMON A B`, DELETE of a line not there, `,BAD`, a string for a COMMON array's count, a range backwards, `B(` with nothing after it |

And typed sessions (`NAME.typ`): `c_tok`, CHAIN of a tokenised and of an ASCII SAVE, from a program and typed, with a
line and with ALL, of a missing file; `c_list`, LIST and LLIST to a file (a range; read back, LOADed again), a
missing name and a drive past P:.

Where PC-BASIC doesn't do what the 1983 source does (`NAME.differ`): `c_ul` (Undefined line number, with no line: a
text file is read by MAIN), `c_miss` (the file is opened first, so ON ERROR traps File not found), `c_e5` (a COMMON
array's count is evaluated: a string is Illegal function call), `c_e7` (and nothing there is Missing operand),
`c_list` (LLIST goes on into LIST, so it lists to the file; `Q:` is Bad file name on the OO-1).

By hand in test_gwbasic_files: CHAIN MERGE of a tokenised file (Bad file mode, where PC-BASIC fails), and
VARPTR(#n), the address of file n's FDB in P4's table (its F_MODE byte), Bad file number for a file not open.

7c's devices as files (LPT1:, COM1:, SCRN:, KYBD:, CONS:, the COM statement): PC-BASIC's printer and ports aren't the
oracle's, so test_gwbasic_files checks their bytes by hand from the source; only the COM statement's errors run here.

| Program | What |
|---|---|
| `d_com` | `COM(n) ON`, `STOP`, `OFF` for 1, 2 and 0 (the class); 3 and 200 Illegal function call (200 passes the source's signed compare: G10's guard); `COM(1) GOSUB` a Syntax error; `ON COM(n) GOSUB`; COM2: Device Unavailable |
