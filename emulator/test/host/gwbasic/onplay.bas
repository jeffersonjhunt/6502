10 REM ON PLAY(n) GOSUB: an event on the OO-1 (7f; the source has none), no trap without PLAY ON
20 PRINT "a"
30 ON PLAY(1) GOSUB 100
40 PRINT "after"
50 END
100 PRINT "trap":RETURN
