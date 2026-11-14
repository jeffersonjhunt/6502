10 REM a function sees its own parameters, not its caller's
20 X=5
30 DEF FNO(Y)=Y+X
40 DEF FNQ(X)=FNO(X*10)+X
50 PRINT FNQ(2);X
