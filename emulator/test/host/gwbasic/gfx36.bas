5 REM 7e4's G35: PAINT's background (p. 243) and the viewport's GET and PUT
10 ON ERROR GOTO 900: DIM R$(200): N=0
20 SCREEN 1: CLS: T$=CHR$(&H5F)+CHR$(&H5F)+CHR$(&H27)+CHR$(&H81)
30 LINE (0,0)-(40,20),2,B: PAINT (10,10),CHR$(&H5F),2: E=0: PAINT (10,10),T$,2: X0=0: X1=40: R$(N)="no bg err"+STR$(E): N=N+1: GOSUB 780
40 CLS: LINE (0,0)-(40,20),2,B: PAINT (10,10),CHR$(&H5F),2: E=0: PAINT (10,10),T$,2,CHR$(&H5F): R$(N)="bg err"+STR$(E): N=N+1: GOSUB 780
50 CLS: LINE (0,0)-(40,20),2,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&H55)+CHR$(&H55)+CHR$(&HAA),2,CHR$(&H55): R$(N)="555555AA err"+STR$(E): N=N+1
60 CLS: LINE (0,0)-(40,20),2,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&HAA)+CHR$(&H55)+CHR$(&H55),2,CHR$(&H55): R$(N)="55AA5555 err"+STR$(E): N=N+1
70 CLS: LINE (0,0)-(40,20),2,B: E=0: PAINT (10,10),CHR$(&H55)+CHR$(&H55)+CHR$(&HAA),2,CHR$(&H55): R$(N)="5555AA err"+STR$(E): N=N+1
80 CLS: LINE (0,0)-(40,20),2,B: PAINT (10,10),CHR$(&H55),2: E=0: PAINT (10,10),CHR$(&HAA)+CHR$(&H55),2: R$(N)="aa55 over 55 err"+STR$(E): N=N+1: GOSUB 780
90 CLS: LINE (0,0)-(40,20),2,B: PAINT (10,10),CHR$(&H55),2: E=0: PAINT (10,10),CHR$(&HFF)+CHR$(&H55)+CHR$(&H55),2: R$(N)="ff5555 over 55 err"+STR$(E): N=N+1: GOSUB 780
100 CLS: LINE (0,0)-(40,20),2,B: PAINT (10,10),CHR$(&H55),2: E=0: PAINT (10,10),CHR$(&HFF)+CHR$(&H55)+CHR$(&H55),2,CHR$(&H55): R$(N)="ff5555 over 55 bg err"+STR$(E): N=N+1: GOSUB 780
110 CLS: LINE (0,0)-(40,20),2,B: E=0: PAINT (10,10),CHR$(&H55),2,CHR$(&H55)+CHR$(&HFF): R$(N)="bg 2 bytes 55ff err"+STR$(E): N=N+1
120 CLS: LINE (0,0)-(40,20),2,B: E=0: PAINT (10,10),CHR$(&H55),2,CHR$(&HAA)+CHR$(&H55): R$(N)="bg 2 bytes aa55 err"+STR$(E): N=N+1
130 CLS: LINE (0,0)-(40,20),2,B: LINE (10,5)-(30,15),2,B: PAINT (5,10),CHR$(&H44),2: X0=0: X1=40: R$(N)="donut": N=N+1: GOSUB 780
140 CLS: DIM A%(200): VIEW (10,10)-(100,100): LINE (0,0)-(20,20),3,BF: E=0: GET (0,0)-(9,9),A%: R$(N)="get rel err"+STR$(E)+STR$(A%(0))+STR$(A%(1))+STR$(A%(2)): N=N+1
150 E=0: PUT (85,85),A%: R$(N)="put past view err"+STR$(E): E=0: PUT (40,40),A%,PSET: R$(N)=R$(N)+" put ok err"+STR$(E): VIEW: R$(N)=R$(N)+STR$(POINT(50,50))+STR$(POINT(49,49))+STR$(POINT(59,59)): N=N+1
160 VIEW (10,10)-(100,100): E=0: GET (0,0)-(150,10),A%: R$(N)="get past view err"+STR$(E): E=0: GET (-1,0)-(5,5),A%: R$(N)=R$(N)+" get neg err"+STR$(E): N=N+1
170 VIEW SCREEN (10,10)-(100,100): E=0: GET (0,0)-(5,5),A%: R$(N)="vs get outside err"+STR$(E): E=0: PUT (0,0),A%: R$(N)=R$(N)+" vs put outside err"+STR$(E): N=N+1
180 CLS: VIEW (10,10)-(101,101): R$(N)="centre 92"+STR$(POINT(0))+STR$(POINT(1)): VIEW (10,10)-(11,11): R$(N)=R$(N)+" 2"+STR$(POINT(0))+STR$(POINT(1)): VIEW (10,10)-(12,12): R$(N)=R$(N)+" 3"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
190 VIEW SCREEN (10,10)-(100,100): R$(N)="vs centre"+STR$(POINT(0))+STR$(POINT(1)): CLS: R$(N)=R$(N)+" after cls"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
200 VIEW (10,10)-(100,100): CIRCLE (45,45),10,3: R$(N)="circle cur"+STR$(POINT(0))+STR$(POINT(1)): N=N+1
210 VIEW (10,10)-(100,100): E=0: X=POINT(95,95): R$(N)="pt 95"+STR$(X): N=N+1
220 SCREEN 2: VIEW (10,10)-(100,100),1: VIEW: R$(N)="s2 fill"+STR$(POINT(9,50))+STR$(POINT(10,50))+STR$(POINT(101,50)): N=N+1
230 SCREEN 1: CLS: VIEW (10,10)-(100,100),,3: VIEW (50,50)-(60,60): VIEW: R$(N)="view 2 keeps border"+STR$(POINT(9,50)): N=N+1
240 SCREEN 0: FOR I=0 TO N-1: PRINT R$(I): NEXT: END
780 FOR Y=1 TO 19: GOSUB 800: R$(N)=STR$(Y)+" "+S$: N=N+1: NEXT: S$="": RETURN
800 S$="": FOR X=X0 TO X1: S$=S$+CHR$(48+POINT(X,Y)): NEXT: RETURN
900 E=ERR: RESUME NEXT
