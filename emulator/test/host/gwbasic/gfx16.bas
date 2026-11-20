10 REM 7e2's G14: PAINT in SCREEN 2 against PC-BASIC: a comb of 160 slots filled from below, a pruned fill of 0 in
15 REM 1, a diamond painted inside and out, the page's second bank (rows 102 on) crossed
20 SCREEN 2
30 FOR X=0 TO 636 STEP 4: LINE (X,0)-(X,90),1: NEXT
40 PAINT (1,95),1
50 LINE (100,120)-(300,190),0,BF: PAINT (150,150),0,1
60 LINE (480,101)-(559,150),0: LINE -(480,199),0: LINE -(401,150),0: LINE -(480,101),0: PAINT (480,150),0,0
70 LINE (330,95)-(400,130),0,B: PAINT (350,110),1,0
9000 DIM A%(8001): GET (0,0)-(639,199),A%: SCREEN 0: WIDTH 80: PRINT A%(0);A%(1)
9010 FOR Y=0 TO 199: R$="": FOR I=0 TO 39: V=A%(2+Y*40+I): L=V AND 255: H=INT(V/256) AND 255
9020 R$=R$+RIGHT$("0"+HEX$(L),2)+RIGHT$("0"+HEX$(H),2): NEXT: PRINT R$: NEXT
