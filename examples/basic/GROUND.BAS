10 REM ====== GROUND - live ground track on a world map ======
20 REM Draws a simple equirectangular world grid (lon -180..180 across,
30 REM lat +90..-90 down) and plots the ACTIVE satellite's sub-point from
40 REM the live SATLAT/SATLON, plus your station from MYLAT/MYLON. Walks the
50 REM loaded catalogue with SATSEL to dot every bird's sub-point too. Pure
60 REM geography - no trig needed. One frame, then SHOW. Pick a satellite on
70 REM Track first; needs POSOK (a fix on where things are) and TIMEOK.
80 REM Vars are single letters A-Z. Colours: 1wht you, 3grn birds, 8gry, 9dgrn.
90 REM Map box: G=left x, H=top y, W=width, D=height (px).
100 LET G = 8
110 LET H = 16
120 LET W = 224
130 LET D = 112
140 CLS
150 REM --- border ---
160 LINE G,H,G+W,H,8
170 LINE G,H+D,G+W,H+D,8
180 LINE G,H,G,H+D,8
190 LINE G+W,H,G+W,H+D,8
200 REM --- equator (E) and prime meridian (P) ---
210 LET E = H + D / 2
220 LINE G,E,G+W,E,9
230 LET P = G + W / 2
240 LINE P,H,P,H+D,9
250 TEXT 2,2,"GROUND TRACK"
260 REM --- your station: a white cross ---
270 LET A = MYLAT
280 LET O = MYLON
290 LET L = 1
300 GOSUB 700
310 IF POSOK = 0 THEN GOTO 600
320 IF TIMEOK = 0 THEN GOTO 600
330 REM --- walk the catalogue, dot each sub-point in green ---
340 LET N = NSAT
350 IF N > 60 THEN N = 60
355 IF N = 0 THEN GOTO 600
360 FOR I = 0 TO N-1
370 SATSEL I
380 IF SATOK = 0 THEN GOTO 430
390 LET A = SATLAT
400 LET O = SATLON
410 LET L = 3
420 GOSUB 800
430 NEXT I
600 REM --- legend ---
610 TEXT 2,130,"wht=you grn=birds"
620 SHOW
630 END
700 REM === plot a cross at lat A lon O, colour L ===
710 GOSUB 900
720 IF X < 0 THEN GOTO 750
730 LINE X-3,Y,X+3,Y,L
740 LINE X,Y-3,X,Y+3,L
750 RETURN
800 REM === plot a small dot at lat A lon O, colour L ===
810 GOSUB 900
820 IF X < 0 THEN GOTO 850
830 PSET X,Y,L
840 CIRCLE X,Y,1,L
850 RETURN
900 REM === map lat A / lon O -> screen X,Y (X=-1 if off-map) ===
910 LET X = 0 - 1
920 IF A > 90 THEN GOTO 980
930 IF A < 0 - 90 THEN GOTO 980
940 IF O > 180 THEN GOTO 980
950 IF O < 0 - 180 THEN GOTO 980
960 LET X = G + (O + 180) / 360 * W
970 LET Y = H + (90 - A) / 180 * D
980 RETURN
