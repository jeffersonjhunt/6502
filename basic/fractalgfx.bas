10 REM The Mandelbrot set in GW-BASIC's graphics: SCREEN 1, 320 x 200
20 REM fractal.bas's picture, a pixel for each point, coloured by how soon it escapes.
30 REM The set is symmetric about its axis, so each row is drawn twice. Any key stops it.
40 DEFINT I,K,X,Y
50 KEY OFF: SCREEN 1: COLOR 0,1: CLS
60 FOR Y=0 TO 99
70 CB=(Y-99.5)*.01: S=CB*CB: S4=S/4
80 FOR X=0 TO 319
90 CA=X*.01117-1.786
100 REM The main cardioid and the bulb to its left are in the set: no need to iterate
110 P=CA-.25: Q=P*P+S: IF Q*(Q+P)<=S4 THEN 300
120 IF (CA+1)*(CA+1)+S<=.0625 THEN 300
130 A=CA: B=CB
140 FOR I=1 TO 16
150 AA=A*A: BB=B*B: IF AA+BB>4 THEN 200
160 B=2*A*B+CB: A=AA-BB+CA
170 NEXT I
180 GOTO 300
190 REM Escaped after I steps: black far out, then cyan, magenta and white nearer the set
200 K=0: IF I>2 THEN K=1: IF I>4 THEN K=2: IF I>8 THEN K=3
210 IF K THEN PSET (X,Y),K: PSET (X,199-Y),K
300 NEXT X
310 IF INKEY$<>"" THEN 400
320 NEXT Y
330 IF INKEY$="" THEN 330
400 SCREEN 0: WIDTH 80: KEY ON
410 END
