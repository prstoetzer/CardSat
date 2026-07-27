10 REM ====== SUNMOON - sun & moon dial with sidereal time ======
20 REM Draws a horizon dial (horizon line across the middle, sky above) and
30 REM places the Sun and Moon by their live azimuth and elevation. Azimuth
40 REM sets the left-right position across the sky band; elevation sets the
50 REM height above the horizon. Also prints local sidereal time LSTHR and
60 REM the magnetic declination MAGDECL so you can true-north-correct a
70 REM compass. Needs POSOK and TIMEOK. One frame, then SHOW. Vars are single
80 REM letters (H = horizon y). Colours: 5yel Sun, 6cyn Moon, 8gry, 9dgrn.
100 LET H = 90
110 CLS
120 TEXT 2,2,"SUN & MOON"
130 REM --- ground and horizon ---
140 LINE 0,H,239,H,9
150 LINE 0,H,239,H,1
160 REM az scale marks: N(0) E(90) S(180) W(270) across the width
170 TEXT 6,H+4,"N"
180 TEXT 60,H+4,"E"
190 TEXT 116,H+4,"S"
200 TEXT 172,H+4,"W"
210 TEXT 226,H+4,"N"
220 IF POSOK = 0 THEN GOTO 700
230 IF TIMEOK = 0 THEN GOTO 700
240 REM --- Sun ---
250 LET A = SUNAZ
260 LET Z = SUNEL
270 LET L = 5
280 GOSUB 800
290 REM --- Moon ---
300 LET A = MOONAZ
310 LET Z = MOONEL
320 LET L = 6
330 GOSUB 800
340 REM --- text readouts ---
350 TEXT 2,104,"Sun el"
360 TEXT 54,104,INT(SUNEL)
370 TEXT 90,104,"Moon el"
380 TEXT 150,104,INT(MOONEL)
390 TEXT 2,114,"LST hr"
400 TEXT 54,114,INT(LSTHR)
410 TEXT 90,114,"MagDec"
420 TEXT 150,114,INT(MAGDECL)
430 TEXT 2,124,"az across, el = height"
440 SHOW
450 END
700 TEXT 2,24,"Need location + clock."
710 TEXT 2,36,"Set them, then run again."
720 SHOW
730 END
800 REM === place a body: az A (0..360), el Z (deg), colour L ===
810 REM X from azimuth across 8..232; Y from elevation up from horizon
820 LET X = 8 + A / 360 * 224
830 IF Z < 0 THEN GOTO 900
840 LET Y = H - Z / 90 * (H - 12)
850 CIRCLE X,Y,4,L
860 CIRCLE X,Y,3,L
870 LINE X,Y,X,H,8
880 RETURN
900 REM below horizon: a hollow marker on the horizon line
910 CIRCLE X,H,2,8
920 RETURN
