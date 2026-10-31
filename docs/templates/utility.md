# Template: a command or utility

One entry for each built-in command and each transient utility, in the User's Guide's chapter for it. Copy the
block below. The headings are fixed, in this order: `check_docs.py` fails an entry that leaves one out, renames
one or moves one. Only **Examples** may be left out.

- **The anchor** is `util-` and the name in lower case (`util-dir`), on its own line just before the heading.
  A built-in and the utility of the same name (DIR and DIR.COM) share one entry.
- **The heading** is `### NAME: what it does`, with the long name (ERASE, not ERA).
- **Forms:** each form of the command line, one a line in a code block, `{}` around what may be left out.
- **Options:** each option and what it does, or "None."
- **Output:** what it prints, laid out as it prints it.
- **Errors:** each message, and what causes it.
- **Since:** `OODOS major.minor`, or "Not yet."
- **Status:** `**built**` or `**proposed**`.
- **CP/M 3:** how it differs from CP/M 3's command, or "None." Our own words.
- **Examples** (optional): a session, as typed and as answered.

```markdown
<a id="util-name"></a>
### NAME: what it does

#### Forms

#### Options

#### Output

#### Errors

#### Since

#### Status

#### CP/M 3

#### Examples <!-- optional -->
```
