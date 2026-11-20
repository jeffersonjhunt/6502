5 REM 7e4's G34: LINE's style (the manual's pp. 156-159)
10 ON ERROR GOTO 900: DIM R$(60): N=0
20 SCREEN 1: CLS
30 LINE (0,0)-(31,0),3,,&HF0F0: Y=0: X0=0: X1=31: GOSUB 800: R$(N)="lr F0F0 "+S$: N=N+1
40 LINE (31,2)-(0,2),3,,&HFF00: Y=2: GOSUB 800: R$(N)="rl FF00 "+S$: N=N+1
50 LINE (0,4)-(31,4),3,,&H8001: Y=4: GOSUB 800: R$(N)="lr 8001 "+S$: N=N+1
60 LINE (0,6)-(31,6),3,,&HC000: Y=6: GOSUB 800: R$(N)="lr C000 "+S$: N=N+1
70 LINE (100,40)-(100,10),3,,&HFF00: X=100: Y0=10: Y1=40: GOSUB 850: R$(N)="up FF00 "+S$: N=N+1
80 LINE (110,10)-(110,40),3,,&HFF00: X=110: GOSUB 850: R$(N)="down FF00 "+S$: N=N+1
90 LINE (0,20)-(10,20),3,,&HF000: LINE -(20,20),3,,&HF000: Y=20: X0=0: X1=20: GOSUB 800: R$(N)="two segs "+S$: N=N+1
100 LINE (0,30)-(10,30),3,,&HF000: LINE (11,30)-(20,30),3,,&HF000: Y=30: GOSUB 800: R$(N)="two lines "+S$: N=N+1
110 LINE (40,40)-(60,50),3,B,&HF0F0
120 Y=40: X0=40: X1=60: GOSUB 800: R$(N)="box top "+S$: N=N+1
130 Y=50: GOSUB 800: R$(N)="box bot "+S$: N=N+1
140 X=40: Y0=40: Y1=50: GOSUB 850: R$(N)="box left "+S$: N=N+1
150 X=60: GOSUB 850: R$(N)="box right "+S$: N=N+1
160 E=0: LINE (0,60)-(10,60),3,BF,&HAAAA: R$(N)="BF style err"+STR$(E): N=N+1
170 E=0: LINE (0,62)-(15,62),3,,65535: Y=62: X0=0: X1=15: GOSUB 800: R$(N)="65535 err"+STR$(E)+" "+S$: N=N+1
180 E=0: LINE (0,64)-(15,64),3,,-1: Y=64: GOSUB 800: R$(N)="-1 err"+STR$(E)+" "+S$: N=N+1
190 E=0: LINE (0,66)-(15,66),3,,65536: Y=66: GOSUB 800: R$(N)="65536 err"+STR$(E)+" "+S$: N=N+1
200 E=0: LINE (0,68)-(15,68),3,,-32768: Y=68: GOSUB 800: R$(N)="-32768 err"+STR$(E)+" "+S$: N=N+1
210 E=0: LINE (0,70)-(15,70),3,,0: Y=70: GOSUB 800: R$(N)="0 err"+STR$(E)+" "+S$: N=N+1
220 E=0: LINE (0,72)-(15,72),3,,-32769: Y=72: GOSUB 800: R$(N)="-32769 err"+STR$(E)+" "+S$: N=N+1
230 E=0: LINE (0,74)-(15,74),,,&HAAAA: Y=74: GOSUB 800: R$(N)="null col err"+STR$(E)+" "+S$: N=N+1
240 E=0: LINE (0,76)-(15,76),3,&HAAAA: R$(N)="2 args err"+STR$(E): N=N+1
250 E=0: LINE (0,78)-(15,78),3,,: R$(N)="trailing comma err"+STR$(E): N=N+1
260 E=0: LINE (0,80)-(15,80),3,,"A": R$(N)="string style err"+STR$(E): N=N+1
270 LINE (-8,90)-(15,90),3,,&HF0F0: Y=90: X0=0: X1=15: GOSUB 800: R$(N)="clip left F0F0 "+S$: N=N+1
280 LINE (15,92)-(-8,92),3,,&HF0F0: Y=92: GOSUB 800: R$(N)="clip rl F0F0 "+S$: N=N+1
290 LINE (0,100)-(31,107),3,,&HF0F0: S$="": FOR X=0 TO 31: FOR Y=100 TO 107: IF POINT(X,Y)=3 THEN S$=S$+"("+MID$(STR$(X),2)+","+MID$(STR$(Y),2)+")"
300 NEXT Y,X: R$(N)="shallow "+S$: N=N+1
320 E=0: LINE (0,122)-(15,122),3,,1.6: Y=122: GOSUB 800: R$(N)="1.6 "+S$: N=N+1
330 E=0: LINE (0,124)-(15,124),3,,40000!: Y=124: GOSUB 800: R$(N)="40000 err"+STR$(E)+" "+S$: N=N+1
340 E=0: LINE (5,126)-(5,126),3,,&H7FFF: Y=126: X0=0: X1=10: GOSUB 800: R$(N)="1pt 7FFF "+S$: N=N+1
350 E=0: LINE (60,130)-(40,140),3,B,&HF0F0: Y=130: X0=40: X1=60: GOSUB 800: R$(N)="box swapped top "+S$: N=N+1
360 Y=140: GOSUB 800: R$(N)="box swapped bot "+S$: N=N+1
370 X=40: Y0=130: Y1=140: GOSUB 850: R$(N)="box swapped left "+S$: N=N+1
380 X=60: GOSUB 850: R$(N)="box swapped right "+S$: N=N+1
390 SCREEN 0: FOR I=0 TO N-1: PRINT R$(I): NEXT: END
800 S$="": FOR X=X0 TO X1: S$=S$+CHR$(48+POINT(X,Y)): NEXT: RETURN
850 S$="": FOR Y=Y0 TO Y1: S$=S$+CHR$(48+POINT(X,Y)): NEXT: RETURN
900 E=ERR: RESUME NEXT
