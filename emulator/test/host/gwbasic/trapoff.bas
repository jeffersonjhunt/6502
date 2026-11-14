10 REM ON ERROR GOTO 0 inside the handler: the error is printed
20 ON ERROR GOTO 100
30 PRINT "start"
40 ERROR 11
50 PRINT "no"
100 PRINT "handler";ERR;ERL
110 ON ERROR GOTO 0
120 PRINT "never"
