# GW-BASIC's test programs and the oracle's outputs

The programs test_gwbasic_core types into GW-BASIC on OODOS (hw/oodos.md, "The plan, phase 7": testing), each
`NAME.bas` with `NAME.out`, what it must print. The programs are the project's own.

Each `NAME.out` is PC-BASIC's output for `NAME.bas`, made by `oodos/gwbasic/tools/oracle.sh`:

- PC-BASIC 2.0.7, the wheel `pcbasic-2.0.7-py2.py3-none-any.whl`, sha256
  `586fd172fbe08cb1f4a05c980177abba3e983229c538b9e531f958719a4f90e7`, checked before it is installed;
- in a throwaway container of `python:3.12-slim`, by digest
  `python@sha256:5e2bdb1b0d542d5039cffb8181c65930093949194a65c365dfb1b0fe13a4fa5b`, removed after the run;
- run as `pcbasic -n NAME.bas -q --output=NAME.out`.

The program goes in and only its output comes out, by `docker cp`. Nothing of PC-BASIC (GPLv3) is in the repo:
only these outputs. An `.out` written by hand (an interactive transcript PC-BASIC's `-n` run can't give) says so
in a comment of the test that uses it.

Run the oracle again only when a program changes, and look at the difference before committing it.
