10 REM ====== CLOCK - an analog UTC clock face ======
20 REM Draws a clock face on the 240x135 canvas and sets the hour, minute
30 REM and second hands from the live UTC clock. Angles are in DEGREES, and
40 REM the screen y axis points down, so a hand at angle T from 12 o'clock
50 REM is (cx + r*SIN(T), cy - r*COS(T)). One frame, then SHOW. Needs the
60 REM clock set (TIMEOK); otherwise it draws the face at 12:00:00.
100 LET C = 67 : REM face centre x
110 LET H = 67 : REM face centre y
120 LET Q = 60 : REM face radius
130 CLS
140 CIRCLE C,H,Q,1
150 CIRCLE C,H,Q-1,8
160 REM --- 12 hour ticks ---
170 FOR A = 0 TO 330 STEP 30
180 LET X = C + (Q-2) * SIN(A)
190 LET Y = H - (Q-2) * COS(A)
200 LET U = C + (Q-8) * SIN(A)
210 LET V = H - (Q-8) * COS(A)
220 LINE X,Y,U,V,1
230 NEXT A
240 PSET C,H,1
250 REM --- read the time (default 12:00:00 if no clock) ---
260 LET E = 0
270 LET M = 0
280 LET S = 0
290 IF TIMEOK = 0 THEN GOTO 330
300 LET E = UTCH
310 LET M = UTCM
320 LET S = UTCS
330 REM --- hour hand: 30 deg per hour + 0.5 deg per minute ---
340 LET T = E * 30 + M / 2
350 LET R = Q * 5 / 10
360 LET L = 5
370 GOSUB 700
380 REM --- minute hand: 6 deg per minute ---
390 LET T = M * 6
400 LET R = Q * 8 / 10
410 LET L = 6
420 GOSUB 700
430 REM --- second hand: 6 deg per second, thin red ---
440 LET T = S * 6
450 LET R = Q * 9 / 10
460 LET X = C + R * SIN(T)
470 LET Y = H - R * COS(T)
480 LINE C,H,X,Y,2
490 REM --- digital readout on the right ---
500 TEXT 140,50,"UTC"
510 TEXT 140,64,E
520 TEXT 158,64,":"
530 TEXT 164,64,M
540 TEXT 182,64,":"
550 TEXT 188,64,S
560 SHOW
570 END
700 REM --- draw a hand: angle T, length R, colour L (two lines = thicker) ---
710 LET X = C + R * SIN(T)
720 LET Y = H - R * COS(T)
730 LINE C,H,X,Y,L
740 LINE C+1,H,X,Y,L
750 RETURN
