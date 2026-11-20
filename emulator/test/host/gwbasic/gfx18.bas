10 REM 7e2's G15: PAINT's queue in free memory (STREND to FRETOP), here above $4000 (an array across $4000-$7FFF),
15 REM where the aperture is while PAINT draws: the comb's 160 slots, the array read back, FRE(0) the same after
20 SCREEN 2: DIM Q%(3000): FOR I=0 TO 3000: Q%(I)=I XOR 21845: NEXT: P0=VARPTR(Q%(0)): P1=VARPTR(Q%(3000))
25 B=0: F1=0: X=0: F0=FRE(0)
30 FOR X=0 TO 636 STEP 4: LINE (X,0)-(X,90),1: NEXT
40 PAINT (1,95),1
50 B=0: FOR I=0 TO 3000: IF Q%(I)<>(I XOR 21845) THEN B=B+1
60 NEXT: F1=FRE(0)
9000 DIM A%(8001): GET (0,0)-(639,199),A%: SCREEN 0: WIDTH 80: PRINT A%(0);A%(1)
9010 FOR Y=0 TO 199: R$="": FOR I=0 TO 39: V=A%(2+Y*40+I): L=V AND 255: H=INT(V/256) AND 255
9020 R$=R$+RIGHT$("0"+HEX$(L),2)+RIGHT$("0"+HEX$(H),2): NEXT: PRINT R$: NEXT
