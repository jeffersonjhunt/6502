# Template: a BDOS function

One entry for each function number in [`oodos/include/oodos.inc`](../../oodos/include/oodos.inc), in the
Programmer's Guide's [chapter 7](../oodos-programmers-guide/07-function-reference.md), in number order. Copy the
block below. The headings are fixed, in this order: `check_docs.py` fails an entry that leaves one out, renames
one or moves one. Only **Example** may be left out. The worked entry is
[function 9's](../oodos-programmers-guide/07-function-reference.md#bdos-9).

- **The anchor** is `bdos-` and the number in decimal, on its own line just before the heading.
- **The heading** is `### N NAME: what it does`: the number, oodos.inc's name for it, and a few words.
- **Entry** and **Exit** give every register the call reads or sets, as [docs/README.md](../README.md#conventions)
  writes them. Say "not kept" for a register the call may change and says nothing about.
- **Errors** are the return codes and, where the error mode matters, what each mode does.
- **Side effects:** what else changes: the SCB, the DMA address, the console's column, a drive logged in.
- **Cycles:** only a measured figure, and the test that measures it. Otherwise "Not measured."
- **Since:** `OODOS major.minor`, the version that first had the function as it is now. A proposed function has
  "Not yet."
- **Status:** `**built**` (in OODOS.SYS and tested) or `**proposed**` (specified, not built).
- **CP/M 3:** how it differs from CP/M 3's function of the same number, or "None." Our own words: never
  Digital Research's text.
- **Example** (optional): a tested program from [docs/examples](../examples/), shown with a listing marker so the
  check holds the listing to the file.

```markdown
<a id="bdos-N"></a>
### N NAME: what it does

#### Entry

#### Exit

#### Errors

#### Side effects

#### Cycles

#### Since

#### Status

#### CP/M 3

#### Example <!-- optional -->
```
