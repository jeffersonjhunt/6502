10 REM INPUT # of numbers and strings, quoted and not, with commas and line ends
20 OPEN "O",#1,"FI.TXT"
30 PRINT #1,"  12  ,  -3.5E2  ,&H10"
40 PRINT #1,"  unquoted string  ,   next   ,last"
50 PRINT #1,CHR$(34);"quoted, with comma";CHR$(34);"  ,";CHR$(34);"q2";CHR$(34)
60 PRINT #1,"1 2 3"
70 PRINT #1,"a";CHR$(10);"b,c";CHR$(10);CHR$(10);"d"
80 PRINT #1,"7";CHR$(10);"8"
90 PRINT #1,CHR$(34);"q";CHR$(34);"tail,after"
100 PRINT #1,"x y"
110 PRINT #1,CHR$(34);"two";CHR$(13);CHR$(10);"lines";CHR$(34)
120 PRINT #1,"9";CHR$(10);CHR$(13);CHR$(10);"10"
130 PRINT #1,CHR$(34);"no close";
140 PRINT #1,
150 PRINT #1,"rest"
160 CLOSE
170 OPEN "I",#1,"FI.TXT"
180 INPUT #1,A,B,C:PRINT A;B;C
190 INPUT #1,A$,B$,C$:PRINT "<";A$;"><";B$;"><";C$;">"
200 INPUT #1,A$,B$:PRINT "<";A$;"><";B$;">"
210 INPUT #1,X,Y,Z:PRINT X;Y;Z
220 INPUT #1,A$,B$,C$:PRINT "<";A$;"><";B$;"><";C$;">";LEN(A$);LEN(C$)
230 INPUT #1,X,Y:PRINT X;Y
240 INPUT #1,A$,B$:PRINT "<";A$;"><";B$;">"
250 INPUT #1,A$:PRINT "<";A$;">"
260 INPUT #1,A$:PRINT "<";A$;">";LEN(A$)
270 INPUT #1,X,Y:PRINT X;Y
280 INPUT #1,A$:PRINT "<";A$;">";LEN(A$);EOF(1)
290 CLOSE
