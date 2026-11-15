10 REM CLEAR closes every file, its last record written
20 OPEN "O",#1,"FR1.TXT":PRINT #1,"by clear"
30 CLEAR
40 OPEN "I",#1,"FR1.TXT":LINE INPUT #1,A$:PRINT A$
50 CLOSE
