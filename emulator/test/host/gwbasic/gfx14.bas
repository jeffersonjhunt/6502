10 REM 7e2's G13: CIRCLE's arcs and radius lines in SCREEN 1 (the source's point counts): start below and above end
15 REM (the wrap), negative ends (lines to the centre), 0 and 2 x PI, -0, an arc off the screen; LINE -() after
20 SCREEN 1
30 CIRCLE (60,50),40,1,0,1.5708: LINE -(10,10),2
40 CIRCLE (160,50),40,2,1,4: LINE -(140,5),3
50 CIRCLE (260,50),40,3,-.5,-2: LINE -(310,5),1
60 CIRCLE (60,150),40,1,-1,3: LINE -(5,195),3
70 CIRCLE (160,150),30,2,4,1: CIRCLE (160,150),40,3,-5,-.7
80 CIRCLE (260,150),35,1,0,6.283185: CIRCLE (260,150),20,2,-0,-3
90 CIRCLE (300,190),40,3,-.3,-2.5: CIRCLE (100,100),15,1,,-2
9000 DIM A%(8001): GET (0,0)-(319,199),A%: SCREEN 0: WIDTH 80: PRINT A%(0);A%(1)
9010 FOR Y=0 TO 199: R$="": FOR I=0 TO 39: V=A%(2+Y*40+I): L=V AND 255: H=INT(V/256) AND 255
9020 R$=R$+RIGHT$("0"+HEX$(L),2)+RIGHT$("0"+HEX$(H),2): NEXT: PRINT R$: NEXT
