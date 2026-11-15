10 REM WRITE # and what INPUT # reads of it
20 OPEN "O",#1,"FW.TXT"
30 WRITE #1,"plain",1,-2.5,"with, comma"
40 WRITE #1,"",0
50 WRITE #1,
60 A$="quoted "+CHR$(34)+"part":WRITE #1,A$;12;"end"
70 WRITE #1,1E+20,1/3,-1#/3
80 CLOSE
90 OPEN "I",#1,"FW.TXT"
100 WHILE NOT EOF(1):LINE INPUT #1,A$:PRINT "[";A$;"]":WEND
110 CLOSE
120 OPEN "I",#1,"FW.TXT"
130 INPUT #1,A$,X,Y,B$:PRINT "<";A$;">";X;Y;"<";B$;">"
140 INPUT #1,A$,X:PRINT "<";A$;">";X
150 INPUT #1,A$,B$,C$,D$:PRINT "<";A$;"><";B$;"><";C$;"><";D$;">"
160 INPUT #1,X#,Y#,Z#:PRINT X#;Y#;Z#
170 PRINT EOF(1):CLOSE
