10 REM TAB and SPC out of the IBM manual's ranges (pp. 327, 340): SPC(n<0), TAB(n<1) Illegal function call
15 ON ERROR GOTO 90
20 PRINT "ab";SPC(-2);"c"
30 PRINT "abcde";TAB(-1);"f"
40 PRINT "x";TAB(0);"y"
50 PRINT "p";SPC(0);"q";TAB(1);"r"
52 PRINT "s";SPC(300);"t"
54 PRINT "u";TAB(256);"v"
60 END
90 PRINT "err";ERR;ERL:RESUME NEXT
