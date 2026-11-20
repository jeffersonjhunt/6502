5 REM 7e4's G29: drawing clipped to a viewport, VIEW SCREEN, PAINT, GET, PUT and DRAW in one
10 ON ERROR GOTO 900: DIM R$(60): N=0
20 SCREEN 1: CLS: LINE (0,0)-(319,199),2,BF
30 VIEW (10,10)-(100,100),0
40 LINE (-50,50)-(200,50),3: LINE (40,-30)-(40,300),3: LINE (-20,-20)-(200,200),1
50 CIRCLE (45,45),60,3
60 VIEW: S$="": FOR X=0 TO 120 STEP 1: S$=S$+CHR$(48+POINT(X,60)): NEXT: R$(N)="row60 "+S$: N=N+1
70 S$="": FOR Y=0 TO 120: S$=S$+CHR$(48+POINT(50,Y)): NEXT: R$(N)="col50 "+S$: N=N+1
80 S$="": FOR I=0 TO 120: S$=S$+CHR$(48+POINT(I,I)): NEXT: R$(N)="diag "+S$: N=N+1
90 CLS: LINE (0,0)-(319,199),2,BF: VIEW SCREEN (10,10)-(100,100),0: LINE (0,50)-(200,50),3: LINE -(50,0),1: R$(N)="vs cur"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
100 VIEW: S$="": FOR X=0 TO 120: S$=S$+CHR$(48+POINT(X,50)): NEXT: R$(N)="vsrow50 "+S$: N=N+1
110 CLS: VIEW (10,10)-(100,100),,3: LINE (0,0)-(90,90),1,B: PAINT (45,45),2,1: VIEW: S$="": FOR X=0 TO 120: S$=S$+CHR$(48+POINT(X,40)): NEXT: R$(N)="paintbox "+S$: N=N+1
120 CLS: VIEW (10,10)-(100,100),,3: PAINT (45,45),2,3: VIEW: S$="": FOR X=0 TO 120: S$=S$+CHR$(48+POINT(X,40)): NEXT: R$(N)="paintview "+S$: N=N+1
130 S$="": FOR Y=0 TO 120: S$=S$+CHR$(48+POINT(40,Y)): NEXT: R$(N)="paintviewcol "+S$: N=N+1
140 CLS: VIEW (10,10)-(100,100): PAINT (45,45),2,3: VIEW: R$(N)="paintview nobord"+STR$(POINT(9,40))+STR$(POINT(10,40))+STR$(POINT(100,40))+STR$(POINT(101,40))+STR$(POINT(40,101)): N=N+1
150 CLS: VIEW (10,10)-(100,100): E=0: PAINT (200,45),2: R$(N)="paint off view err"+STR$(E)+" cur"+STR$(POINT(0))+STR$(POINT(1)): VIEW: R$(N)=R$(N)+STR$(POINT(50,50)): N=N+1
160 CLS: VIEW (10,10)-(100,100): LINE (0,0)-(20,20),3,BF: E=0: GET (0,0)-(9,9),A%: R$(N)="get rel err"+STR$(E): E=0: PUT (85,85),A%: R$(N)=R$(N)+" put past view err"+STR$(E)
165 E=0: PUT (40,40),A%,PSET: R$(N)=R$(N)+" put ok"+STR$(E): VIEW: R$(N)=R$(N)+STR$(POINT(50,50))+STR$(POINT(49,49))+STR$(POINT(59,59)): N=N+1
170 CLS: VIEW (10,10)-(100,100): E=0: GET (0,0)-(150,10),A%: R$(N)="get past view err"+STR$(E): N=N+1
180 CLS: VIEW (10,10)-(100,100): PSET (0,0),1: DRAW "R10 D10": R$(N)="draw cur"+STR$(POINT(0))+STR$(POINT(1)): VIEW: R$(N)=R$(N)+STR$(POINT(20,20))+STR$(POINT(10,20))+STR$(POINT(15,10)): N=N+1
190 CLS: VIEW (10,10)-(100,100): PSET (0,0),1: DRAW "BM-5,-5 R20": VIEW: R$(N)="draw clip"+STR$(POINT(9,5))+STR$(POINT(12,5))+STR$(POINT(5,5)): N=N+1
200 CLS: VIEW (10,10)-(100,100): PSET (50,50),2: PRINT "AB";: X1=POINT(0): R$(N)="point0 rel"+STR$(X1): N=N+1
210 CLS: VIEW (10,10)-(100,100): PSET (5,5),3: R$(N)="pmap noW"+STR$(PMAP(5,0))+STR$(PMAP(5,1))+STR$(PMAP(5,2))+STR$(PMAP(5,3))+" pt"+STR$(POINT(0))+STR$(POINT(1))+STR$(POINT(2))+STR$(POINT(3)): N=N+1
220 CLS: VIEW (10,10)-(100,100): R$(N)="cls in view home"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
230 SCREEN 0: FOR I=0 TO N-1: PRINT R$(I): NEXT: END
900 E=ERR: RESUME NEXT
