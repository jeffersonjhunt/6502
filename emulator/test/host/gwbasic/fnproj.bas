10 REM a function whose result is its string parameter, copied before PARM1 is put back
20 DEF FNU$(U$)=U$
30 B$="ab"
40 PRINT FNU$(B$)+FNU$("cd")+FNU$(B$+"x");B$
50 C$=FNU$(B$):B$="zz":PRINT C$;B$;FNU$(FNU$("ef"))
