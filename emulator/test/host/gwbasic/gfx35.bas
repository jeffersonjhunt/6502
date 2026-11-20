5 REM 7e4's G35: PAINT's tiles in SCREEN 1 and 2 (the manual's pp. 236-245)
10 ON ERROR GOTO 900: DIM R$(120): N=0
20 SCREEN 1: CLS: LINE (0,0)-(40,20),3,B
30 PAINT (10,10),CHR$(&H1B)+CHR$(&HE4)+CHR$(&H00),3: X0=0: X1=40: FOR Y=0 TO 7: GOSUB 800: R$(N)="s1 tile y"+STR$(Y)+" "+S$: N=N+1: NEXT
40 CLS: LINE (3,1)-(40,20),3,B: PAINT (10,10),CHR$(&H1B),3: X0=0: X1=20: Y=5: GOSUB 800: R$(N)="s1 offset "+S$: N=N+1
50 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&HAA): X0=0: X1=40: Y=5: GOSUB 800: R$(N)="s1 nobound err"+STR$(E)+" "+S$: Y=21: GOSUB 800: R$(N)=R$(N)+" below "+S$: N=N+1
60 CLS: LINE (0,0)-(40,20),2,B: E=0: PAINT (10,10),CHR$(&HFF): X0=0: X1=40: Y=5: GOSUB 800: R$(N)="s1 nobound ff err"+STR$(E)+" "+S$: N=N+1
70 CLS: E=0: PAINT (10,10),"": R$(N)="empty tile err"+STR$(E): N=N+1
80 CLS: LINE (0,0)-(40,20),3,B: E=0: T$=STRING$(64,&H55): PAINT (10,10),T$,3: R$(N)="64 err"+STR$(E)+STR$(POINT(10,10)): N=N+1
90 CLS: LINE (0,0)-(40,20),3,B: E=0: T$=STRING$(65,&H55): PAINT (10,10),T$,3: R$(N)="65 err"+STR$(E)+STR$(POINT(10,10)): N=N+1
100 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55),3,CHR$(&H55): R$(N)="bg = tile1 err"+STR$(E)+STR$(POINT(10,10)): N=N+1
110 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&H55),3,CHR$(&H55): R$(N)="bg = 2 err"+STR$(E)+STR$(POINT(10,10)): N=N+1
120 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&H55)+CHR$(&H55),3,CHR$(&H55): R$(N)="bg = 3 err"+STR$(E)+STR$(POINT(10,10)): N=N+1
130 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&HAA)+CHR$(&H55)+CHR$(&H55)+CHR$(&HAA),3,CHR$(&H55): R$(N)="bg 2 consec err"+STR$(E)+STR$(POINT(10,10)): N=N+1
140 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&HAA)+CHR$(&HAA),3,CHR$(&H55): R$(N)="bg wrap 1+? err"+STR$(E)+STR$(POINT(10,10)): N=N+1
150 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&HAA),3,CHR$(&H55)+CHR$(&HAA): R$(N)="bg 2 bytes err"+STR$(E)+STR$(POINT(10,10)): N=N+1
160 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55),3,"": R$(N)="bg empty err"+STR$(E)+STR$(POINT(10,10)): N=N+1
170 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55),3,5: R$(N)="bg num err"+STR$(E): N=N+1
180 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),1,3,CHR$(&H55): R$(N)="bg w/ num paint err"+STR$(E)+STR$(POINT(10,10)): N=N+1
190 CLS: LINE (0,0)-(40,20),3,B: E=0: PAINT (10,10),CHR$(&H55),,CHR$(&H00): R$(N)="bg no bound err"+STR$(E)+STR$(POINT(10,10)): N=N+1
200 REM manual example: painted area already tiled
205 T$=CHR$(&H5F)+CHR$(&H5F)+CHR$(&H27)+CHR$(&H81)
210 CLS: LINE (0,0)-(40,20),3,B: PAINT (10,10),CHR$(&H5F),3: E=0: PAINT (10,10),T$,3: X0=0: X1=40: GOSUB 780: R$(N)="no bg "+S$+" err"+STR$(E): N=N+1
220 CLS: LINE (0,0)-(40,20),3,B: PAINT (10,10),CHR$(&H5F),3: E=0: PAINT (10,10),T$,3,CHR$(&H5F): X0=0: X1=40: GOSUB 780: R$(N)="bg "+S$+" err"+STR$(E): N=N+1
230 CLS: E=0: PAINT (400,10),CHR$(&H55),3: R$(N)="tile off screen err"+STR$(E)+" cur"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
240 CLS: LINE (0,0)-(40,20),3,B: PAINT (10,10),CHR$(&H55),3: E=0: PAINT (10,10),CHR$(&H55),3: R$(N)="repaint same err"+STR$(E): N=N+1
250 CLS: LINE (0,0)-(40,20),3,B: PAINT (10,10),CHR$(&HFF),3: R$(N)="tile ff = bound"+STR$(POINT(10,10))+STR$(POINT(10,15)): N=N+1
260 CLS: LINE (0,0)-(40,20),1,B: PAINT (10,10),CHR$(&HFF)+CHR$(&H00),1: X0=0: X1=40: Y=5: GOSUB 800: R$(N)="ff00 y5 "+S$: Y=6: GOSUB 800: R$(N)=R$(N)+" y6 "+S$: N=N+1
270 CLS: VIEW (10,10)-(60,60),,3: PAINT (20,20),CHR$(&H1B),3: VIEW: X0=0: X1=70: Y=15: GOSUB 800: R$(N)="view tile "+S$: N=N+1
280 SCREEN 2: CLS: LINE (0,0)-(40,20),1,B: PAINT (10,10),CHR$(&H81)+CHR$(&H42)+CHR$(&H24),1: X0=0: X1=40: FOR Y=0 TO 5: GOSUB 800: R$(N)="s2 y"+STR$(Y)+" "+S$: N=N+1: NEXT
290 CLS: LINE (3,1)-(40,20),1,B: PAINT (10,10),CHR$(&HF0),1: X0=0: X1=24: Y=5: GOSUB 800: R$(N)="s2 offset "+S$: N=N+1
300 CLS: LINE (0,0)-(40,20),1,B: E=0: PAINT (10,10),CHR$(&HAA)+CHR$(&HAA)+CHR$(&HAA),1,CHR$(&HAA): R$(N)="s2 bg3 err"+STR$(E): N=N+1
310 CLS: E=0: PAINT (10,10),CHR$(&H55),1: R$(N)="s2 whole screen err"+STR$(E)+STR$(POINT(10,10))+STR$(POINT(11,10))+STR$(POINT(600,199)): N=N+1
320 SCREEN 0: FOR I=0 TO N-1: PRINT R$(I): NEXT: END
780 FOR Y=1 TO 19: GOSUB 800: R$(N)=STR$(Y)+" "+S$: N=N+1: NEXT: S$="": RETURN
800 S$="": FOR X=X0 TO X1: S$=S$+CHR$(48+POINT(X,Y)): NEXT: RETURN
900 E=ERR: RESUME NEXT
