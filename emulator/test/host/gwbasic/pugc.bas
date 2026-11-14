10 REM the format's characters moved by collections while its list is evaluated (a string space of about 700 bytes)
20 F=FRE(0):DIM Z#(INT((F-700)/8))
30 A$="[&]":B$="<!>"
40 FOR I=1 TO 30
50 PRINT USING A$+B$;LEFT$(STRING$(200,"x")+"y",1+I MOD 5);RIGHT$(STRING$(200,"q")+STR$(I),2);MID$(STRING$(150,"m")+"n",150)
60 NEXT
70 PRINT USING "\  \";STRING$(250,"a")+"b"
