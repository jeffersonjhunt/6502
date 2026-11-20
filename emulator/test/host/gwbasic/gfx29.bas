5 REM 7e4's G29 and G30: VIEW's viewport, its border and fill, its errors and the cursor (the IBM manual's pp. 357-360)
10 ON ERROR GOTO 900: DIM R$(60): N=0
20 SCREEN 1: CLS
30 VIEW (10,10)-(100,100),1,2
40 GOSUB 800: R$(N)="after view cursor"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
50 PSET (0,0),3: PSET (95,95),3: PSET (-5,-5),3: P1=POINT(0,0): P2=POINT(95,95): P3=POINT(90,90): P4=POINT(-1,-1): P5=POINT(-11,-11)
60 R$(N)="rel points"+STR$(P1)+STR$(P2)+STR$(P3)+STR$(P4)+STR$(P5)+" cur"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
70 VIEW: A1=POINT(9,9): A2=POINT(10,10): A3=POINT(100,100): A4=POINT(101,101): A5=POINT(50,50): A6=POINT(105,105): A7=POINT(9,50): A8=POINT(101,50): A9=POINT(50,101)
80 R$(N)="abs 9,9="+STR$(A1)+" 10,10="+STR$(A2)+" 100,100="+STR$(A3)+" 101,101="+STR$(A4)+" 50,50="+STR$(A5)+" 105="+STR$(A6)+" bord"+STR$(A7)+STR$(A8)+STR$(A9): N=N+1
90 R$(N)="after VIEW alone cursor"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
100 CLS: VIEW SCREEN (10,10)-(100,100),,2: PSET (0,0),3: PSET (10,10),3: PSET (50,50),1: Q1=POINT(0,0): Q2=POINT(10,10): Q3=POINT(9,9): Q4=POINT(101,101)
110 VIEW: R$(N)="vs pts"+STR$(Q1)+STR$(Q2)+STR$(Q3)+STR$(Q4)+" abs"+STR$(POINT(0,0))+STR$(POINT(10,10))+STR$(POINT(50,50))+STR$(POINT(9,9)): N=N+1
120 CLS: VIEW (0,0)-(100,100),,2: VIEW: R$(N)="edge border"+STR$(POINT(0,0))+STR$(POINT(101,50))+STR$(POINT(50,101))+STR$(POINT(101,101))+STR$(POINT(0,50)): N=N+1
130 CLS: VIEW (219,99)-(319,199),,2: VIEW: R$(N)="edge border2"+STR$(POINT(218,150))+STR$(POINT(250,98))+STR$(POINT(218,98)): N=N+1
140 CLS: VIEW (100,100)-(5,5),,1: VIEW: R$(N)="sorted"+STR$(POINT(4,4))+STR$(POINT(101,101))+STR$(POINT(4,50)): N=N+1
150 E=0: VIEW (0,0)-(320,100): R$(N)="x320 err"+STR$(E): N=N+1
160 E=0: VIEW (5,5)-(5,50): R$(N)="x1=x2 err"+STR$(E): N=N+1
170 E=0: VIEW (-1,5)-(50,50): R$(N)="neg err"+STR$(E): N=N+1
180 E=0: VIEW (5,5)-(50,200): R$(N)="y200 err"+STR$(E): N=N+1
190 E=0: VIEW (5,5)-(50,50),4: R$(N)="fill4 err"+STR$(E): N=N+1
200 E=0: VIEW (5,5)-(50,50),,4: R$(N)="bord4 err"+STR$(E): N=N+1
210 E=0: VIEW (5,5)-(50,50),,,: R$(N)="3commas err"+STR$(E): N=N+1
220 E=0: VIEW (5,5): R$(N)="one pt err"+STR$(E): N=N+1
230 E=0: VIEW STEP(5,5)-(50,50): R$(N)="step err"+STR$(E): N=N+1
240 E=0: VIEW (5,5)-STEP(50,50): R$(N)="step2 err"+STR$(E): N=N+1
250 E=0: VIEW SCREEN: R$(N)="view screen alone err"+STR$(E): N=N+1
260 E=0: VIEW (0,0)-(319,199): R$(N)="full ok err"+STR$(E): N=N+1
270 E=0: VIEW (1.6,1.4)-(50.5,50.5),3: VIEW: R$(N)="round err"+STR$(E)+STR$(POINT(1,1))+STR$(POINT(2,1))+STR$(POINT(1,2))+STR$(POINT(50,50))+STR$(POINT(51,51))+STR$(POINT(50,51)): N=N+1
280 E=0: VIEW (5,5)-(50,50),-1: R$(N)="fill -1 err"+STR$(E): N=N+1
290 CLS: E=0: VIEW (5,5)-(50,50),255: VIEW: R$(N)="fill 255 err"+STR$(E)+STR$(POINT(20,20)): N=N+1
300 CLS: VIEW (10,10)-(100,100): PSET(20,20),2: CLS: R$(N)="cls cursor"+STR$(POINT(0))+STR$(POINT(1)): VIEW: R$(N)=R$(N)+" pix"+STR$(POINT(30,30)): N=N+1
310 SCREEN 0: FOR I=0 TO N-1: PRINT R$(I): NEXT: END
800 RETURN
900 E=ERR: RESUME NEXT
