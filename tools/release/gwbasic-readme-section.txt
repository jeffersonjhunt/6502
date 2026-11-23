GW-BASIC is on this card too. At A>, type GWBASIC; at its Ok:
  FILES "*.BAS"   the BASIC programs on A:
  RUN "FIB"       load a program and run it
  LOAD "PI"       load one, then LIST to read it and RUN to run it
  SYSTEM          back to A>
  F1 LIST, F2 RUN F1-F10 on the bottom line; KEY OFF or Ctrl-T hides it

The programs (some ask for their numbers when they run):
  FIB, SUPERFIB   Fibonacci numbers, as long as you like, by strings
  ADDBIG          adds two numbers of any length
  PI              the digits of pi, by a spigot
  MONTEPI         pi by random darts, Monte Carlo style
  CPI             points on the unit circle
  FRACTAL         the Mandelbrot set in digits, on the text screen
  FRACTALG        the Mandelbrot set in SCREEN 1, slowly; a key stops it
  LIFE            Conway's Game of Life on the screen; a key stops it
  LIFEGFX         the Game of Life in SCREEN 1's graphics; a key stops it
  INTEREST        a loan's payments

Try the graphics (at Ok, one line at a time; SCREEN 0 brings the text back):
  SCREEN 1: CIRCLE (160,100),60,2      a circle of radius 60, mid-screen
  PAINT (160,100),1,2                  fill it, up to its colour-2 edge
  DRAW "C3 BM100,100 R40 D40 L40 U40"  a square, by pen moves
  DRAW "TA45 R40"                      a line turned 45 degrees
  DIM A%(230): GET (140,80)-(180,120),A%  a 41 x 41 piece of it into A%
  PUT (20,20),A%,XOR                   put it at 20,20; the same PUT erases it
  LINE (0,190)-(319,190),3,,&HF0F0     a dashed line, by its style
  PAINT (120,120),CHR$(&HAA)+CHR$(&H55),3  the square, filled with a tile
  VIEW (60,40)-(259,159),0,3: CIRCLE (100,60),80,2  the circle cut to the box

SCREEN 10, 11 and 12 are the OO-1's own modes, not IBM BASIC's:
  SCREEN 10: FOR I=0 TO 255: LINE (I,0)-(I,239),I: NEXT  all 256 colours
  SCREEN 11: CIRCLE (256,240),200,14: PAINT (256,240),9,14  16 colours, filled
  SCREEN 12: CIRCLE (320,240),200: LINE (0,0)-(639,479)  640 x 480, mono

Sound, on the sound card (with the sound on):
  BEEP                                 a short beep
  SOUND 440,18                         440 Hz for about a second
  PLAY "T120 O3 L8 CDEFGAB > C"        a scale, in PLAY's music language
  PLAY "MB L4 EDCDEEE2"                MB plays on while BASIC goes on

Events (in a program; the trap is taken between statements):
  ON TIMER(2) GOSUB 100: TIMER ON      GOSUB 100 every 2 seconds
  ON KEY(1) GOSUB 200: KEY(1) ON       F1 calls 200 while it runs
  ON PLAY(2) GOSUB 300: PLAY ON        300 as MB's queue drops below 2 notes
  PRINT TIMER, TIME$, DATE$            the seconds since midnight, the clock
ON COM isn't trapped yet.

