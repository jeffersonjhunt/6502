1 REM @memsize 1500
10 REM the garbage collector, in a small space: thousands of strings made and
20 REM dropped, some kept in variables and an array, all checked at the end
30 DIM K$(9) : I=0
40 K$(I)=STR$(I)+"-"+SPACE$(I*3)+"*" : I=I+1 : IF I<10 THEN 40
50 N=0 : A$="KEEP"
60 T$=LEFT$(SPACE$(40)+STR$(N),45)+CHR$(65+N MOD 26) : U$=T$+T$ : J=N MOD 10
70 IF N MOD 7=0 THEN K$(J)=MID$(U$,40,10)+K$(J)
80 IF LEN(K$(J))>60 THEN K$(J)=RIGHT$(K$(J),30)
90 B$=A$+STR$(N) : C$=MID$(B$,5)
100 N=N+1 : IF N<1000 THEN 60
110 I=0
120 PRINT K$(I);"|";LEN(K$(I)) : I=I+1 : IF I<10 THEN 120
130 PRINT T$;LEN(U$);A$;B$;C$;VAL(C$)
140 X=FRE("") : PRINT A$;B$;K$(9)
150 D$="" : M=0
160 D$=D$+CHR$(48+M MOD 10) : M=M+1 : IF M<200 THEN 160
170 PRINT LEN(D$);LEFT$(D$,12);RIGHT$(D$,5);INSTR(D$,"789012")
