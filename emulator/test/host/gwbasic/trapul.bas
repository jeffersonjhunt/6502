10 REM ON ERROR GOTO a line that isn't there: Undefined line number
20 PRINT "a"
30 ON ERROR GOTO 1000
40 PRINT "no"
