10 REM ====== MANDEL - the Mandelbrot set, coarse ======
20 REM Escape-time Mandelbrot over the classic view, drawn as coloured blocks.
30 REM For each point c = X + iY it iterates z = z*z + c until |z| > 2 or the
40 REM iteration cap, then colours by how fast it escaped. Pure maths, no
50 REM system data. Coarse on purpose (a 3-pixel grid, cap 12) so it stays
60 REM inside the interpreter's work budget - it still takes a few seconds.
70 REM One frame, then SHOW.
100 LET W = 3 : REM pixel step (bigger = faster, coarser)
110 LET T = 12 : REM iteration cap
120 CLS
130 FOR I = 0 TO 237 STEP W
140 REM map screen x (0..239) to cx in -2.2 .. 1.0
150 LET A = 0 - 2.2 + I / 239 * 3.2
160 FOR J = 0 TO 132 STEP W
170 REM map screen y (0..134) to cy in -1.3 .. 1.3
180 LET B = 0 - 1.3 + J / 134 * 2.6
190 LET X = 0
200 LET Y = 0
210 LET K = 0
220 REM --- iterate z = z*z + c ---
230 LET P = X * X
240 LET Q = Y * Y
250 IF P + Q > 4 THEN GOTO 320
260 LET Y = 2 * X * Y + B
270 LET X = P - Q + A
280 LET K = K + 1
290 IF K < T THEN GOTO 230
300 REM fell through: inside the set -> black, skip drawing
310 GOTO 360
320 REM escaped at K: pick a colour band by speed
330 LET L = 1 + K - INT(K / 8) * 8
340 IF L = 0 THEN L = 1
350 LINE I,J,I+W-1,J,L
360 NEXT J
370 NEXT I
380 TEXT 2,2,"MANDELBROT"
390 SHOW
400 END
