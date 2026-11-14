10 REM ON TIMER(n) GOSUB: TIMER is no reserved word in the 1983 source
20 ON TIMER(1) GOSUB 100
30 PRINT "after"
40 END
100 PRINT "trap":RETURN
