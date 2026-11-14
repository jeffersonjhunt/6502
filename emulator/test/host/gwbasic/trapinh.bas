10 REM an error inside the handler: printed, the trap off
20 ON ERROR GOTO 100
30 PRINT 1/0
40 PRINT "no"
100 PRINT "in handler";ERR;ERL
110 X=SQR(-1)
120 RESUME NEXT
