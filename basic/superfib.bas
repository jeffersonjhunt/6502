10 INPUT "Number of terms:"; T
20 IF T<3 THEN PRINT "Generating first 2 terms.": T=2
30 A$="1": B$="1"
40 PRINT "1: "; A$
50 FOR I = 2 TO T
60  PRINT I; ": "; B$
70  GOSUB 1000
80  A$ = B$
90  B$ = R$
100 NEXT I

1000 REM --- BigIntegerAdd using arrays instead of strings ---
1010 REM Input: A$, B$ string numbers
1020 REM Output: digit arrays R(1..RSize) and RSize
1030 ASize = LEN(A$)
1040 BSize = LEN(B$)
1050 REM Determine maximum number of digits MaxSize
1060 IF BSize>ASize THEN 1090
1070 MaxSize=ASize:GOTO 1100
1090 MaxSize=BSize
1100 REM Dimension arrays A, B, R
1110 DIM A(MaxSize),B(MaxSize),R(MaxSize+1)
1120 REM Load digits into arrays with zero padding
1130 FOR I=1 TO MaxSize
1140   REM Load A digit or zero
1150   IF I>ASize THEN TEMP=0:GOTO 1170
1160   TEMP=VAL(MID$(A$,I,1))
1170   A(I)=TEMP
1180   REM Load B digit or zero
1190   IF I>BSize THEN TEMP=0:GOTO 1210
1200   TEMP=VAL(MID$(B$,I,1))
1210   B(I)=TEMP
1220 NEXT I
1230 REM Initialize carry
1240 CD=0
1250 REM Add digits from least significant to most
1260 FOR K=MaxSize TO 1 STEP -1
1270   S = A(K) + B(K) + CD
1280   CD = INT(S/10)
1290   R(K+1) = S - CD*10
1300 NEXT K
1310 REM Store final carry
1320 R(1) = CD
1330 REM Strip leading zeros to find start index SI
1340 SI = 1
1350 IF SI<=MaxSize AND R(SI)=0 THEN SI=SI+1:GOTO 1350
1360 REM Compute result size
1370 RSize = MaxSize + 2 - SI
1380 REM Shift result digits to start at index 1
1390 FOR J=1 TO RSize
1400   R(J) = R(SI+J-1)
1410 NEXT J
1420 RETURN
