10 REM ====== STARFLD - a projected 3-D star field ======
20 REM Seeds a handful of stars from DATA statements (x, y, z triples) with
30 REM READ, then draws each one perspective-projected onto the screen:
40 REM sx = cx + x/z * F, sy = cy - y/z * F. Nearer stars (small z) land
50 REM bigger and brighter. Demonstrates DATA / READ / RESTORE and simple
60 REM 3-D maths - no system data, always runs. One frame, then SHOW.
70 REM Colours 0-9: 1wht 5yel 6cyn 8gry.
100 LET C = 120 : REM screen centre x
110 LET H = 67 : REM screen centre y
120 LET F = 90 : REM focal length (bigger = more zoom)
130 CLS
140 TEXT 2,2,"STARFIELD"
150 REM --- number of stars, then that many x,y,z triples ---
160 READ K
170 FOR I = 1 TO K
180 READ X
190 READ Y
200 READ Z
210 IF Z <= 0 THEN GOTO 300
220 REM perspective project
230 LET U = C + X / Z * F
240 LET V = H - Y / Z * F
250 IF U < 0 THEN GOTO 300
260 IF U > 239 THEN GOTO 300
270 IF V < 0 THEN GOTO 300
280 IF V > 134 THEN GOTO 300
290 GOSUB 700
300 NEXT I
310 REM --- a few connecting rays from centre to the nearest stars ---
320 TEXT 2,124,"DATA/READ 3-D projection"
330 SHOW
340 END
700 REM === draw one star: size and colour from depth Z (in globals) ===
710 LET R = 0
720 IF Z < 6 THEN R = 1
730 IF Z < 3 THEN R = 2
740 LET L = 8 : REM far = grey
750 IF Z < 6 THEN L = 6 : REM mid = cyan
760 IF Z < 3 THEN L = 1 : REM near = white
765 IF R > 0 THEN GOTO 785
770 PSET U,V,L
775 GOTO 790
785 CIRCLE U,V,R,L
790 RETURN
800 REM ================= star data =================
810 REM first value = count; then x,y,z for each star
820 DATA 18
830 DATA -3,2,2,  1,3,3,  4,-1,2,  -2,-3,4,  2,1,1
840 DATA 5,4,5,  -5,1,3,  0,-2,2,  3,3,6,  -4,-2,3
850 DATA 1,-4,4,  -1,4,7,  6,0,8,  -6,-1,5,  2,-2,2
860 DATA 4,2,3,  -3,3,5,  0,0,1
