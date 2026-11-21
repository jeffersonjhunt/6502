10 REM ON TIMER(n) GOSUB: an event on the OO-1 (7f; the source has none), no trap without TIMER ON
20 ON TIMER(1) GOSUB 100
30 PRINT "after"
40 END
100 PRINT "trap":RETURN
