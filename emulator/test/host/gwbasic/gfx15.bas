10 REM 7e2's G14: PAINT in SCREEN 1, the source's algorithm (ADVGRP.ASM), against PC-BASIC (lines 9000 on dump it)
15 REM (shapes of lines whose major deltas are odd, or 45 degrees, where PC-BASIC's lines are the source's):
16 REM a boundary colour, the default (the paint's), nested boxes, a one-pixel leak, a pruned box, start on the border
20 SCREEN 1
30 LINE (60,11)-(99,50),3: LINE -(60,89),3: LINE -(21,50),3: LINE -(60,11),3: PAINT (60,50),1,3
40 LINE (130,10)-(180,60),2,B: PAINT (150,30),3,2
50 LINE (200,10)-(240,40),1,B: PAINT (220,20),1
60 LINE (250,10)-(310,70),2,B: LINE (260,20)-(300,60),2,B: PAINT (255,40),3,2: PAINT (280,40),1,2
70 LINE (10,100)-(100,190),2,B: LINE (11,145)-(99,145),1: PAINT (50,120),1,2
80 LINE (110,100)-(200,190),2,B: LINE (111,145)-(150,145),1: PAINT (150,120),1,2
90 LINE (210,100)-(300,140),3,B: PSET (255,140),0: LINE (230,150)-(280,190),3,B: PAINT (255,120),2,3
100 LINE (61,75)-(120,95),2: LINE -(90,60),2: LINE -(61,75),2: PAINT (90,80),3,2: PAINT (300,195),2,2
110 LINE (130,70)-(180,90),2,B: PAINT (130,80),1,2: PAINT (155,80),0,2
9000 DIM A%(8001): GET (0,0)-(319,199),A%: SCREEN 0: WIDTH 80: PRINT A%(0);A%(1)
9010 FOR Y=0 TO 199: R$="": FOR I=0 TO 39: V=A%(2+Y*40+I): L=V AND 255: H=INT(V/256) AND 255
9020 R$=R$+RIGHT$("0"+HEX$(L),2)+RIGHT$("0"+HEX$(H),2): NEXT: PRINT R$: NEXT
