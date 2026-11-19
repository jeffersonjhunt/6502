10 REM Conway's Game of Life on GW-BASIC's text screen (SCREEN 0)
20 REM A cell is two character cells, 39 x 23 of them under a status line.
30 REM The board wraps at its edges. Any key stops it. LIFEGFX.BAS draws it in graphics.
40 DEFINT A-Z
50 W=39: H=23
60 DIM G(40,24), N(40,24)
70 A$=CHR$(219)+CHR$(219): D$="  "
80 PRINT "Conway's Game of Life"
90 PRINT "1 random   2 two gliders   3 R-pentomino"
100 INPUT "Pattern";P
110 IF P<1 OR P>3 THEN 100
120 IF P=1 THEN GOSUB 1600
130 IF P=2 THEN RESTORE 2000: GOSUB 1500
140 IF P=3 THEN RESTORE 2100: GOSUB 1500
150 SCREEN 0: WIDTH 80: CLS
160 FOR Y=1 TO H: FOR X=1 TO W
170 IF G(X,Y) THEN GOSUB 1000
180 NEXT X,Y
190 GN=0
200 REM One generation: the edges wrapped, every cell counted, then changed
210 GN=GN+1: PP=0
220 GOSUB 1200
230 FOR Y=1 TO H: FOR X=1 TO W
240 IF N(X,Y)<>G(X,Y) THEN G(X,Y)=N(X,Y): GOSUB 1000
250 PP=PP+G(X,Y)
260 NEXT X,Y
270 LOCATE 1,1: PRINT "Gen";GN;"  Pop";PP;"  ";
280 IF INKEY$="" THEN 210
290 LOCATE 24,1: PRINT "Stopped at generation";GN;"with";PP;"cells.";
300 END
1000 REM Draw cell X,Y: two solid blocks if alive, two spaces if dead
1010 LOCATE Y+1,2*X-1
1020 IF G(X,Y) THEN PRINT A$; ELSE PRINT D$;
1030 RETURN
1200 REM The next generation into N: wrap the edges, then count each cell's neighbours
1210 FOR Y=1 TO H: G(0,Y)=G(W,Y): G(W+1,Y)=G(1,Y): NEXT Y
1220 FOR X=0 TO W+1: G(X,0)=G(X,H): G(X,H+1)=G(X,1): NEXT X
1230 FOR Y=1 TO H: FOR X=1 TO W
1240 C=G(X-1,Y-1)+G(X,Y-1)+G(X+1,Y-1)+G(X-1,Y)+G(X+1,Y)+G(X-1,Y+1)+G(X,Y+1)+G(X+1,Y+1)
1250 IF C=3 OR (C=2 AND G(X,Y)=1) THEN N(X,Y)=1 ELSE N(X,Y)=0
1260 NEXT X,Y
1270 RETURN
1500 REM A pattern from DATA: how many cells, then each one's X,Y
1510 READ K
1520 FOR I=1 TO K: READ X,Y: G(X,Y)=1: NEXT I
1530 RETURN
1600 REM A random board, about a quarter alive
1610 RANDOMIZE TIMER
1620 FOR Y=1 TO H: FOR X=1 TO W
1630 IF RND<.25 THEN G(X,Y)=1
1640 NEXT X,Y
1650 RETURN
2000 REM Two gliders, heading down and to the right
2010 DATA 10, 6,5, 7,6, 5,7, 6,7, 7,7
2020 DATA 21,10, 22,11, 20,12, 21,12, 22,12
2100 REM The R-pentomino: five cells that run for over a thousand generations on an open plane
2110 DATA 5, 21,12, 22,12, 20,13, 21,13, 21,14
