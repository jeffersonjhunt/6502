10 REM strings: literals, +, the relations, LET's copies
20 A$="HELLO" : B$="WORLD" : C$=A$+", "+B$+"!"
30 PRINT C$;LEN(C$);"[";"";"]";LEN("")
40 PRINT "A"<"B";"B"<"A";"A"="A";"A"<>"A";"AB">"A";"A"<"AB";""<"A";""="";"ABC"<="ABD";"B">="AZ"
50 PRINT "abc">"ABC";"Z"<"a";"A "="A";A$="HELLO";A$+B$="HELLOWORLD";C$>A$
60 D$=A$ : A$=A$+"X" : PRINT D$;" ";A$
70 E$=LEFT$(C$,5) : F$=E$ : E$="CHANGED" : PRINT E$;" ";F$
80 X$="" : Y$=X$+X$ : PRINT LEN(Y$);"<";Y$;">"
90 PRINT "SUM ";1+2;"STR "+"ING"
100 IF A$="HELLOX" THEN PRINT "EQ" ELSE PRINT "NE"
110 IF "A">"B" THEN PRINT "WRONG" ELSE PRINT "RIGHT"
120 G$="QUOTE" : PRINT G$;G$;G$
130 PRINT Q$;"<>";LEN(Q$);Q$=""
140 H$="12345678901234567890123456789012345678901234567890"
150 H$=H$+H$+H$+H$+H$ : PRINT LEN(H$);RIGHT$(H$,3)
