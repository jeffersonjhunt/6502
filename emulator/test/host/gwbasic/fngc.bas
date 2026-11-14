10 REM strings collected while string functions' parameters are bound (a string space of about 900 bytes)
20 DEF FNY$(K)=LEFT$(STRING$(200,"y")+"z",0)
30 DEF FNG$(P$,Q$)=FNY$(1)+FNY$(2)+P$+FNY$(3)+FNY$(4)+"/"+Q$+P$
40 DEF FNO$(R$)=FNG$(R$+"a","b"+R$)+"|"+R$
50 N=0:E=0:I=0:A$="":B$="":C$="":F=FRE(0)
60 DIM Z#(INT((F-900)/8))
70 FOR I=1 TO 100
80 A$="x"+STR$(I):B$=FNO$(A$)
90 C$=A$+"a/b"+A$+A$+"a|"+A$
100 IF B$<>C$ THEN E=E+1:IF E<4 THEN PRINT I;B$;" ";C$
110 NEXT I
120 PRINT "errors";E;B$
