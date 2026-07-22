10 REM ====== HARMONO - a harmonograph curve ======
20 REM Traces the path of a damped two-pen harmonograph: two sine terms per
30 REM axis at slightly detuned frequencies, each fading with an EXP decay.
40 REM Pure maths - no system data - so it always draws. Trig is in DEGREES.
50 REM The angle A sweeps a long arc; change the frequency and phase constants
60 REM below for a different figure. One frame, then SHOW.
100 LET C = 120 : REM centre x
110 LET H = 67 : REM centre y
120 CLS
130 REM --- x = decaying sum of two sines; y likewise, detuned ---
140 REM frequencies (deg multipliers) and phases:
150 LET P = 2 : REM x freq 1
160 LET Q = 3 : REM x freq 2
170 LET R = 3 : REM y freq 1
180 LET S = 2 : REM y freq 2
190 LET F = 0 : REM 'have previous point' flag
200 FOR A = 0 TO 1440 STEP 4
210 LET E = EXP(0 - A / 900) : REM global decay 1 -> ~0.2
220 LET X = C + E * 46 * SIN(P * A + 10) + E * 30 * SIN(Q * A)
230 LET Y = H + E * 40 * SIN(R * A) + E * 22 * SIN(S * A + 40)
240 IF F = 0 THEN GOTO 280
250 REM colour drifts through the palette as it decays
260 LET L = 2 + INT(A / 240)
270 LINE U,V,X,Y,L
280 LET U = X
290 LET V = Y
300 LET F = 1
310 NEXT A
320 TEXT 2,2,"HARMONO"
330 SHOW
340 END
