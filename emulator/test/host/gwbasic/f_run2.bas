10 REM RUN closes a file left open: its last record on the disk, read from another number
20 OPEN "O",#1,"FR3.TXT":PRINT #1,"the last record";
30 RUN 100
100 OPEN "I",#3,"FR3.TXT":LINE INPUT #3,A$:PRINT A$:CLOSE
