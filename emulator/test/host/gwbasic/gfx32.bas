5 REM 7e4's G31: CIRCLE, DRAW, GET and PAINT under a WINDOW, VIEW with WINDOW, the resets
10 ON ERROR GOTO 900: DIM R$(80): N=0
20 SCREEN 1
250 CLS: WINDOW (-6,-6)-(6,6): CIRCLE (0,0),1,3: WINDOW: XA=130: XB=190: YA=60: YB=140: GOSUB 700: R$(N)="circle r1 w6 "+B$: N=N+1
260 CLS: WINDOW (-6,-6)-(6,6): CIRCLE (0,0),1,3,,,1: WINDOW: XA=130: XB=190: YA=60: YB=140: GOSUB 700: R$(N)="circle r1 asp1 "+B$: N=N+1
270 CLS: WINDOW (0,0)-(1,1): PSET (.5,.5),1: DRAW "R10": WINDOW: XA=150: XB=200: YA=95: YB=105: GOSUB 700: R$(N)="draw under window "+B$: N=N+1
280 CLS: VIEW (10,10)-(109,109): WINDOW (0,0)-(1,1): R$(N)="vw pmap"+STR$(PMAP(0,0))+STR$(PMAP(1,0))+STR$(PMAP(0,1))+STR$(PMAP(1,1))+STR$(PMAP(10,2))+STR$(PMAP(0,2))
285 PSET (1,1),3: R$(N)=R$(N)+" pt"+STR$(POINT(0))+STR$(POINT(1))+STR$(POINT(2))+STR$(POINT(3)): VIEW: WINDOW: R$(N)=R$(N)+" abs"+STR$(POINT(109,10))+STR$(POINT(110,9))+STR$(POINT(108,11)): R$(N)=R$(N)+" "+B$: N=N+1
290 CLS: WINDOW (0,0)-(1,1): VIEW (10,10)-(109,109): R$(N)="wv pmap"+STR$(PMAP(1,0))+STR$(PMAP(1,1)): N=N+1
300 CLS: WINDOW (0,0)-(100,100): LINE (0,0)-(100,100),3,B: R$(N)="box cur"+STR$(POINT(0))+STR$(POINT(1))+STR$(POINT(2))+STR$(POINT(3)): N=N+1
310 CLS: WINDOW (0,0)-(1,1): CLS: R$(N)="cls under window"+STR$(POINT(0))+STR$(POINT(1))+STR$(POINT(2))+STR$(POINT(3))+STR$(PMAP(1,0)): N=N+1
330 WINDOW (0,0)-(1,1): SCREEN 2: R$(N)="screen 2 resets?"+STR$(PMAP(1,0)): SCREEN 1: N=N+1
340 WINDOW (0,0)-(1,1): WIDTH 80: R$(N)="width resets?"+STR$(PMAP(1,0)): SCREEN 1: N=N+1
350 CLS: DIM A%(100): WINDOW (0,0)-(10,10): GET (1,1)-(2,2),A%: R$(N)="get w"+STR$(A%(0))+STR$(A%(1)): N=N+1
360 CLS: WINDOW (0,0)-(10,10): PAINT (5,5),1: R$(N)="paint w"+STR$(POINT(0))+STR$(POINT(1))+STR$(POINT(2))+STR$(POINT(3)): N=N+1
370 CLS: WINDOW (0,0)-(10,10): E=0: X=POINT(5,5): R$(N)="point world"+STR$(X): PSET (5,5),2: X=POINT(5,5): R$(N)=R$(N)+STR$(X)+STR$(POINT(11,11))+STR$(POINT(-.01,5)): N=N+1
380 SCREEN 0: E=0: X=PMAP(1,0): R$(N)="screen0 pmap err"+STR$(E)+STR$(X): E=0: WINDOW (0,0)-(1,1): R$(N)=R$(N)+" window err"+STR$(E)
385 E=0: VIEW (0,0)-(1,1): R$(N)=R$(N)+" view err"+STR$(E): E=0: X=POINT(0): R$(N)=R$(N)+" point0 err"+STR$(E): N=N+1
390 FOR I=0 TO N-1: PRINT R$(I): NEXT: END
700 B$="": X0=999: X1=-1: Y0=999: Y1=-1: C=0: FOR Y=YA TO YB: FOR X=XA TO XB: P=POINT(X,Y): IF P>0 THEN C=C+1: IF X<X0 THEN X0=X
710 IF P>0 THEN IF X>X1 THEN X1=X
720 IF P>0 THEN IF Y<Y0 THEN Y0=Y
730 IF P>0 THEN IF Y>Y1 THEN Y1=Y
740 NEXT X,Y: B$="n"+STR$(C)+" x"+STR$(X0)+STR$(X1)+" y"+STR$(Y0)+STR$(Y1): RETURN
900 E=ERR: RESUME NEXT
