10 REM ====== SKYDOME - a live all-sky radar in CardSat Tiny BASIC ======
20 REM Plots the sky as a polar dome: horizon = outer ring, zenith = the
30 REM centre, North = up. Draws elevation rings and a compass rose, marks
40 REM the Sun and Moon from live data, then walks the loaded satellite
50 REM catalogue with SATSEL and dots every bird at its real az/el. A little
60 REM 3-petal rose exercises the trig. Trig is in DEGREES, so az/el drop
70 REM straight in. One frame, then SHOW (no INPUT). One statement per line;
80 REM ' : REM' adds a trailing comment. Colours 0-9: 0blk 1wht 2red 3grn
90 REM 4blu 5yel 6cyn 7org 8gry 9dgrn. Paste in Settings > BASIC, then Run.
100 C = 175 : REM dome centre x (data column on the right)
110 H = 67 : REM dome centre y = zenith
120 Q = 62 : REM horizon radius in pixels (el = 0)
130 CLS
140 REM --- three elevation rings + the zenith dot ---
150 CIRCLE C,H,Q,1
160 E = 30
170 GOSUB 1100
180 E = 60
190 GOSUB 1100
200 PSET C,H,1
210 REM --- eight compass spokes ---
220 FOR A = 0 TO 315 STEP 45
230 S = C + 8 * SIN(A)
240 T = H - 8 * COS(A)
250 U = C + Q * SIN(A)
260 V = H - Q * COS(A)
270 LINE S,T,U,V,8
280 NEXT A
290 TEXT C-2,H-Q-9,"N"
300 TEXT C+Q+3,H-3,"E"
310 TEXT C-2,H+Q+2,"S"
320 TEXT C-Q-9,H-3,"W"
330 REM --- header and live UTC clock ---
340 TEXT 4,2,"CardSat SKYDOME"
350 IF TIMEOK = 0 THEN GOTO 420
360 TEXT 150,2,UTCH
370 TEXT 167,2,":"
380 TEXT 173,2,UTCM
390 TEXT 191,2,":"
400 TEXT 197,2,UTCS
420 REM --- Sun (yellow) then Moon (cyan) ---
430 Z = SUNEL
440 A = SUNAZ
450 L = 5
460 GOSUB 1200
470 Z = MOONEL
480 A = MOONAZ
490 L = 6
500 GOSUB 1200
520 REM --- walk the catalogue; dot each bird at its az/el ---
530 N = NSAT
540 IF N > 60 THEN N = 60
550 B = 0 : REM count of birds above the horizon
560 IF POSOK = 0 THEN GOTO 700
570 IF TIMEOK = 0 THEN GOTO 700
580 FOR I = 0 TO N-1
590 SATSEL I
600 IF SATOK = 0 THEN GOTO 660
610 Z = SATEL
620 A = SATAZ
630 L = 3
640 IF Z >= 0 THEN B = B + 1
650 GOSUB 1200
660 NEXT I
700 REM --- readouts along the bottom ---
710 TEXT 2,116,"Up:"
720 TEXT 26,116,B
730 TEXT 2,126,"Of:"
740 TEXT 26,126,N
750 IF SPWXOK = 0 THEN GOTO 820
760 TEXT 60,126,"SFI"
770 TEXT 86,126,SFI
780 TEXT 120,126,"Kp"
790 TEXT 140,126,KP
820 REM --- a 3-petal rose, bottom-left, pure trig ---
830 J = 26
840 K = 98
850 F = 0
860 FOR A = 0 TO 360 STEP 5
865 R = 10 * COS(3 * A)
870 IF R < 0 THEN R = 0 - R
875 U = J + R * SIN(A)
880 W = K - R * COS(A)
885 IF F = 0 THEN GOTO 895
890 LINE S,D,U,W,4
895 S = U
900 D = W
905 F = 1
910 NEXT A
915 SHOW
920 END
1100 REM === subroutine: elevation ring for E degrees ===
1110 R = Q * (90 - E) / 90
1120 CIRCLE C,H,R,8
1130 RETURN
1200 REM === subroutine: sky marker at az A, el Z, colour L ===
1210 IF Z >= -2 THEN GOTO 1230
1220 RETURN
1230 G = Z
1240 IF G < 0 THEN G = 0
1250 R = Q * (90 - G) / 90
1260 X = C + R * SIN(A)
1270 Y = H - R * COS(A)
1280 CIRCLE X,Y,2,L
1290 IF Z < 0 THEN GOTO 1320
1300 PSET X,Y,L
1310 CIRCLE X,Y,1,L
1320 RETURN
