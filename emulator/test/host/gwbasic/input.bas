10 REM INPUT and LINE INPUT: prompts, several values, ?Redo from start
20 INPUT A:PRINT A
30 INPUT "Name";N$:PRINT "Hi ";N$
40 INPUT "Two, no ? ",X,Y:PRINT X+Y
50 INPUT A$,B%,C:PRINT A$;B%;C
60 INPUT D$:PRINT "[";D$;"]"
70 LINE INPUT L$:PRINT "[";L$;"]"
80 LINE INPUT "Line: ";L$:PRINT "[";L$;"]"
90 INPUT E#:PRINT E#
100 INPUT F%:PRINT F%
110 DIM V(3):INPUT V(1),V(2+1):PRINT V(1);V(3)
120 INPUT "last";Z:PRINT Z
