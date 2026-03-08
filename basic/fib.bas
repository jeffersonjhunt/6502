10 INPUT "Number of terms (2-255):"; T
20 IF T<3 THEN PRINT "Generating first 2 terms.": T=2
30 A$="1": B$="1"
40 PRINT "1: "; A$
50 FOR I = 2 TO T
60  PRINT I; ": "; B$
70  GOSUB 1000
80  A$ = B$
90  B$ = R$
100 NEXT I

999  END
1000  REM  --- BigIntegerAdd Subroutine (A$,B$ -> R$) ---
1010 Z$ = "0123456789"
1020 LA =  LEN (A$):LB =  LEN (B$)
1030 P$ = "": FOR J = 1 TO LB - LA:P$ = P$ + "0": NEXT J
1040 A$ = P$ + A$
1050 LA =  LEN (A$):P$ = "": FOR J = 1 TO LA - LB:P$ = P$ + "0": NEXT J
1060 B$ = P$ + B$
1070 R$ = "":CD = 0
1080  FOR K = LA TO 1 STEP  - 1
1090 D1 =  VAL ( MID$ (A$,K,1)):D2 =  VAL ( MID$ (B$,K,1))
1100 S = D1 + D2 + CD
1110 CD =  INT (S / 10):D = S - CD * 10
1120 R$ =  MID$ (Z$,D + 1,1) + R$
1130  NEXT K
1140  IF CD <  > 0 THEN R$ =  MID$ (Z$,CD + 1,1) + R$
1145  REM  strip leading zeros (but leave one zero if result is zero)
1150  IF  LEN (R$) > 1 AND  LEFT$ (R$,1) = "0" THEN R$ =  RIGHT$ (R$, LEN (R$) - 1): GOTO 1150
1160  RETURN
