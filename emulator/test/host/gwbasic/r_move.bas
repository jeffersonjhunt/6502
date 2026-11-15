10 DIM S$(3):FOR I=0 TO 3:S$(I)="str"+STR$(I):NEXT:T$="simple":U$=T$+"!"
20 OPEN "R",#1,"RM1.DAT",30:FIELD #1,30 AS A$:LSET A$="first file"
30 OPEN "R",#2,"RM2.DAT",40:FIELD #2,20 AS B$,20 AS C$:LSET B$="second":RSET C$="file"
40 PRINT T$;U$;S$(0);S$(3);"|";LEFT$(A$,10);"|";B$;C$
50 CLOSE #1:PRINT LEN(A$);LEFT$(B$,6);RIGHT$(C$,4);T$;U$;S$(2)
60 OPEN "R",#3,"RM3.DAT",50:FIELD #3,50 AS D$:LSET D$="third":PUT #3,1:PUT #2,1
70 CLOSE #2:PRINT LEN(B$);LEN(C$);LEFT$(D$,5);S$(1);U$
80 GET #3,1:PRINT LEFT$(D$,5):CLOSE:PRINT LEN(D$)
90 OPEN "R",#1,"RM2.DAT",40:FIELD #1,20 AS B$,20 AS C$:GET #1,1
100 PRINT B$;"|";C$:CLOSE
