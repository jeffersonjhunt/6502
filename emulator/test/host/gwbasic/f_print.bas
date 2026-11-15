10 REM PRINT # with commas, semicolons, TAB, SPC and USING
20 OPEN "O",#1,"FP.TXT"
30 PRINT #1,"a","b","c"
40 PRINT #1,1,-2,3.5
50 PRINT #1,"x";"y";1;-1;"z"
60 PRINT #1,TAB(10);"t10";TAB(3);"t3"
70 PRINT #1,SPC(5);"s5";SPC(0);"s0"
80 PRINT #1,USING "##.##";3.14159;2.5
90 PRINT #1,USING "&!";"left";"right"
100 PRINT #1,"no new line";
110 PRINT #1," then one"
120 PRINT #1,
130 PRINT #1,1E+20;1D-10;123456789#
140 PRINT #1,"long",,"gap"
150 CLOSE
160 OPEN "I",#1,"FP.TXT"
170 WHILE NOT EOF(1):LINE INPUT #1,A$:PRINT "[";A$;"]":WEND
180 CLOSE
