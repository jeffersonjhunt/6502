10 REM the frame: lines run in order, GOTO, IF, variables kept
20 A=5 : B=A*2 : PRINT "A";A;"B";B
30 IF A>3 THEN 50
40 PRINT "not here"
50 C$="ab"+"cd" : PRINT C$;LEN(C$)
60 I%=0
70 I%=I%+1 : IF I%<100 THEN 70
80 PRINT I%;
90 ? "done"
