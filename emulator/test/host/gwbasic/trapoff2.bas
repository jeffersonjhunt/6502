10 REM ON ERROR GOTO 0 outside the handler: the trap off, the next error printed
20 ON ERROR GOTO 100
30 ON ERROR GOTO 0
40 PRINT "start"
50 X=SQR(-1)
60 PRINT "no"
100 PRINT "never"
110 RESUME NEXT
