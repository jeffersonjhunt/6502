10 A$="abcdefgh"
20 MID$(A$,3)="XY":PRINT A$
30 MID$(A$,7)="12345":PRINT A$
40 MID$(A$,1,2)="pqrs":PRINT A$
50 MID$(A$,2,10)="Z":PRINT A$
60 MID$(A$,8)="!":PRINT A$
70 MID$(A$,4,0)="nothing":PRINT A$
80 MID$(A$,5)="":PRINT A$
90 B$="literal":C$=B$:MID$(B$,1)="L":PRINT B$;" ";C$
100 DIM D$(2):D$(1)="array":MID$(D$(1),2,3)="RRA":PRINT D$(1)
110 E$="x"+"yz":MID$(E$,2)="Q":PRINT E$
120 READ F$:MID$(F$,1)="D":PRINT F$:RESTORE:READ G$:PRINT G$
130 DATA data
140 H$="abc":MID$(H$,3)="xyz":PRINT H$
150 MID$(H$,1)="j"+"k":PRINT H$
160 MID$(H$,2.6,1.5)=STRING$(3,"w"):PRINT H$
