5 REM 7e4's G31 and G32: WINDOW and PMAP (the manual's pp. 256-257, 370-374), POINT(n) (pp. 258-260)
10 ON ERROR GOTO 900: DIM R$(80): N=0
20 SCREEN 1: CLS: WINDOW (-1,-1)-(1,1)
30 R$(N)="pmap"+STR$(PMAP(-1,0))+STR$(PMAP(-1,1))+STR$(PMAP(1,0))+STR$(PMAP(1,1))+STR$(PMAP(0,0))+STR$(PMAP(0,1))+STR$(PMAP(.5,0))+STR$(PMAP(.5,1)): N=N+1
40 R$(N)="pmap inv"+STR$(PMAP(0,2))+STR$(PMAP(319,2))+STR$(PMAP(0,3))+STR$(PMAP(199,3))+STR$(PMAP(160,2))+STR$(PMAP(100,3))+STR$(PMAP(1,2)): N=N+1
50 PSET (0,0),3: R$(N)="pset00 pt"+STR$(POINT(0))+STR$(POINT(1))+STR$(POINT(2))+STR$(POINT(3)): N=N+1
60 R$(N)="after window cursor?": N=N+1
70 WINDOW SCREEN (-1,-1)-(1,1): R$(N)="ws pmap"+STR$(PMAP(-1,0))+STR$(PMAP(-1,1))+STR$(PMAP(1,0))+STR$(PMAP(1,1))+STR$(PMAP(0,1))+STR$(PMAP(199,3)): N=N+1
80 WINDOW: SCREEN 1: CLS: PSET (5,15): WINDOW (0,0)-(319,199): R$(N)="manual a": FOR I=0 TO 3: R$(N)=R$(N)+STR$(POINT(I)): NEXT: N=N+1
90 PSET (5,15): R$(N)="manual b": FOR I=0 TO 3: R$(N)=R$(N)+STR$(POINT(I)): NEXT: N=N+1
100 WINDOW SCREEN (0,0)-(319,199): PSET (5,15): R$(N)="manual c": FOR I=0 TO 3: R$(N)=R$(N)+STR$(POINT(I)): NEXT: N=N+1
110 WINDOW (0,0)-(3,3): R$(N)="frac": FOR X=0 TO 3 STEP .25: R$(N)=R$(N)+STR$(PMAP(X,0)): NEXT: N=N+1
120 R$(N)="fracy": FOR X=0 TO 3 STEP .25: R$(N)=R$(N)+STR$(PMAP(X,1)): NEXT: N=N+1
130 WINDOW (0,0)-(10,10): R$(N)="ten": FOR X=0 TO 10: R$(N)=R$(N)+STR$(PMAP(X,0)): NEXT: N=N+1
140 R$(N)="teny": FOR X=0 TO 10: R$(N)=R$(N)+STR$(PMAP(X,1)): NEXT: N=N+1
160 CLS: WINDOW (0,0)-(10,10): FOR X=0 TO 10: PSET (X,X),3: NEXT: WINDOW: S$="": FOR X=0 TO 319: YC=199-INT(X*199/319): FOR Y=YC-3 TO YC+3: IF POINT(X,Y)=3 THEN S$=S$+"("+MID$(STR$(X),2)+","+MID$(STR$(Y),2)+")"
165 NEXT Y,X: R$(N)="pix "+S$: N=N+1
170 CLS: WINDOW (0,0)-(10,10): PSET (2,2),1: PSET STEP(1,1),3: R$(N)="step"+STR$(POINT(0))+STR$(POINT(1))+STR$(POINT(2))+STR$(POINT(3)): N=N+1
180 E=0: WINDOW (5,5)-(5,6): R$(N)="x1=x2 err"+STR$(E): N=N+1
190 E=0: WINDOW (5,5)-(6,5): R$(N)="y1=y2 err"+STR$(E): N=N+1
200 E=0: WINDOW (0,0)-(1,1): LINE (-1000,-1000)-(1000,1000),3: R$(N)="huge line err"+STR$(E): N=N+1
210 E=0: WINDOW (0,0)-(1,1): PSET (1000,1000),3: R$(N)="huge pset err"+STR$(E)+" pt"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
220 E=0: R$(N)="pmap huge"+STR$(PMAP(1000,0)): R$(N)=R$(N)+" err"+STR$(E): N=N+1
230 E=0: R$(N)="pmap n4": X=PMAP(1,4): R$(N)=R$(N)+" err"+STR$(E): E=0: X=PMAP(1,-1): R$(N)=R$(N)+" n-1 err"+STR$(E): E=0: X=PMAP(1,2.6): R$(N)=R$(N)+" n2.6 err"+STR$(E)+STR$(X): N=N+1
240 E=0: X=POINT(4): R$(N)="point4 err"+STR$(E): E=0: X=POINT(-1): R$(N)=R$(N)+" point-1 err"+STR$(E): E=0: X=POINT(1.6): R$(N)=R$(N)+" point1.6"+STR$(X)+" err"+STR$(E): N=N+1
245 SCREEN 0
390 FOR I=0 TO N-1: PRINT R$(I): NEXT: END
700 B$="": X0=999: X1=-1: Y0=999: Y1=-1: C=0: FOR Y=YA TO YB: FOR X=XA TO XB: P=POINT(X,Y): IF P>0 THEN C=C+1: IF X<X0 THEN X0=X
710 IF P>0 THEN IF X>X1 THEN X1=X
720 IF P>0 THEN IF Y<Y0 THEN Y0=Y
730 IF P>0 THEN IF Y>Y1 THEN Y1=Y
740 NEXT X,Y: B$="n"+STR$(C)+" x"+STR$(X0)+STR$(X1)+" y"+STR$(Y0)+STR$(Y1): RETURN
900 E=ERR: RESUME NEXT
