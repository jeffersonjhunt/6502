10 REM Big integer addition in AppleSoft BASIC
20 REM   (pure string arithmetic, no IF…THEN for padding)
30 INPUT "Enter first number:";A$
40 INPUT "Enter second number:";B$
50 REM Digit lookup string (only legal one-letter variable is Z$, so we’ll reuse it)
60 Z$="0123456789"
70 REM Get original lengths
80 LA=LEN(A$)
90 LB=LEN(B$)
100 REM Pad A$ up to length LB (loop runs 0 times if LB<=LA)
110 P$=""
120 FOR I=1 TO LB-LA
130   P$=P$+"0"
140 NEXT I
150 A$=P$+A$
160 REM Pad B$ up to the new length of A$ (which is now max of the two)
170 LA=LEN(A$)
180 P$=""
190 FOR I=1 TO LA-LB
200   P$=P$+"0"
210 NEXT I
220 B$=P$+B$
230 REM Now both strings are the same length LA
240 R$=""
250 CD=0
260 REM Add digit by digit from right to left
270 FOR I=LA TO 1 STEP -1
280   D1=VAL(MID$(A$,I,1))
290   D2=VAL(MID$(B$,I,1))
300   S=D1+D2+CD
310   CD=INT(S/10)
320   D=S-CD*10
330   CH$=MID$(Z$,D+1,1)
340   R$=CH$+R$
350 NEXT I
360 REM Prepend final carry if nonzero
370 IF CD=0 THEN 400
380 CH$=MID$(Z$,CD+1,1)
390 R$=CH$+R$
400 REM Print the result
410 PRINT "Sum = ";R$
420 END
