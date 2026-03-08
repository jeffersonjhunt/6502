10 REM Spigot algorithm to compute digits of Pi in AppleSoft BASIC
20 REM Ask user for number of digits
30 INPUT "Number of digits to calculate: "; N
40 REM Compute required array size P = INT((10 * N) / 3) + 1
50 P = INT((10 * N) / 3) + 1
60 REM Allocate workspace array A and output buffer PV$
70 DIM A(P)
80 DIM PV$(N)
90 REM Initialize output string to empty
100 PV$ = ""
110 REM Initialize all slots of A to 2
120 FOR I = 1 TO P
130   A(I) = 2
140 NEXT I
150 REM Blank line before output
160 PRINT
170 REM Main loop: generate N digits
180 FOR J = 1 TO N
190   REM Reset carry for this digit
200   C = 0
210   REM Process array from P down to 2
220   FOR I = P TO 2 STEP -1
230     X = (A(I) * 10) + C
240     Q = INT(X / ((2 * I) - 1))
250     A(I) = X - (Q * ((2 * I) - 1))
260     C = Q * (I - 1)
270   NEXT I
280   REM Handle the first element
290   X = (A(1) * 10) + C
300   D = INT(X / 10)
310   A(1) = X - (D * 10)
320   REM If we ever get a “10” digit, propagate the carry
330   IF D = 10 THEN GOSUB 600
340   REM Append the (possibly adjusted) digit to our buffer
350   PV$ = PV$ + CHR$(48 + D)
360 REM Display each digit as it is calculated
370 PRINT CHR$(48 + D);
380 NEXT J
390 REM Move to new line after streaming digits
400 PRINT
410 END

600 REM Overflow handler: set D=0 and carry into prior digits
610 D = 0
620 KK = LEN(PV$)
630 FOR KK = KK TO 1 STEP -1
640   T = ASC(MID$(PV$,KK,1))
650   IF T < ASC("9") THEN 690
660   REM Current digit is "9", turn it to "0"
670   PV$ = LEFT$(PV$,KK - 1) + "0" + MID$(PV$,KK + 1)
680 NEXT KK
690 REM All prior digits were "9" and have been set to "0"
700 REM Increment the first non-"9" we found (or exit if none)
710 PV$ = LEFT$(PV$,KK - 1) + CHR$(T + 1) + MID$(PV$,KK + 1)
715 REM Trim PV$ to only the digits after the last "8"
720 L8 = 0
730 FOR K = 1 TO LEN(PV$)
740   IF MID$(PV$,K,1) = "8" THEN L8 = K
750 NEXT K
760 IF L8 > 0 THEN PV$ = MID$(PV$,L8 + 1)
770 RETURN
