10 REM a string parameter named as a variable set after the DEF
20 DEF FNU$(A$)=A$+"."
30 A$="ab"
40 PRINT FNU$(A$);FNU$("cd");FNU$(A$+"x");A$
50 DEF FNV(A)=A*2
60 A=3
70 PRINT FNV(A);FNV(10);A
