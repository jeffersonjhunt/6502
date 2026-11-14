10 REM ON PLAY(n) GOSUB: PLAY is a statement, no event, in the 1983 source
20 PRINT "a"
30 ON PLAY(1) GOSUB 100
40 PRINT "after"
50 END
100 PRINT "trap":RETURN
