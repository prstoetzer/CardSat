10 REM ====== SPACEWX - a space-weather dashboard ======
20 REM Reads the live solar/geomagnetic snapshot (SFI, KP, A-index, sunspot
30 REM number, MUF, and the IMF Bz) and draws labelled bar gauges plus a
40 REM one-line "HF conditions" verdict from SFI and Kp. All values come
50 REM from CardSat's space-weather feed, so it needs SPWXOK; if that is 0
60 REM the feed hasn't loaded yet and it says so. One frame, then SHOW.
70 REM No graphics INPUT - just gauges. Colours: 3grn good, 5yel fair,
80 REM 2red poor, 8gry scale.
100 CLS
110 TEXT 2,2,"SPACE WEATHER"
120 IF SPWXOK = 0 THEN GOTO 800
130 REM --- gauge 1: SFI (solar flux) scale 60..300 ---
140 LET Y = 20
150 TEXT 2,Y,"SFI"
160 LET V = SFI
170 LET P = (V - 60) / 240 * 150 : REM px across a 150-wide bar
180 LET L = 2
190 IF V >= 90 THEN L = 5
200 IF V >= 120 THEN L = 3
210 GOSUB 900
220 TEXT 210,Y,INT(V)
230 REM --- gauge 2: Kp index scale 0..9 (lower is calmer) ---
240 LET Y = 36
250 TEXT 2,Y,"Kp"
260 LET V = KP
270 LET P = V / 9 * 150
280 LET L = 3
290 IF V >= 4 THEN L = 5
300 IF V >= 5 THEN L = 2
310 GOSUB 900
320 TEXT 210,Y,INT(V)
330 REM --- gauge 3: A-index scale 0..100 ---
340 LET Y = 52
350 TEXT 2,Y,"A"
360 LET V = AINDEX
370 LET P = V / 100 * 150
380 IF P > 150 THEN P = 150
390 LET L = 3
400 IF V >= 20 THEN L = 5
410 IF V >= 30 THEN L = 2
420 GOSUB 900
430 TEXT 210,Y,INT(V)
440 REM --- gauge 4: sunspot number scale 0..250 ---
450 LET Y = 68
460 TEXT 2,Y,"SSN"
470 LET V = SSN
480 LET P = V / 250 * 150
490 IF P > 150 THEN P = 150
500 LET L = 5
510 IF V >= 80 THEN L = 3
520 GOSUB 900
530 TEXT 210,Y,INT(V)
540 REM --- MUF and Bz as text (Bz south = negative = storm risk) ---
550 TEXT 2,86,"MUF MHz"
560 TEXT 70,86,INT(MUF)
570 TEXT 120,86,"Bz"
580 TEXT 140,86,INT(BZ)
590 REM --- verdict line from SFI and Kp ---
600 LET C = 3 : REM assume good
610 IF SFI < 90 THEN C = 5
620 IF KP >= 4 THEN C = 5
630 IF KP >= 5 THEN C = 2
640 IF SFI < 70 THEN C = 2
650 LINE 2,104,232,104,8
655 IF C = 2 THEN GOTO 673
656 IF C = 5 THEN GOTO 666
657 REM --- else C = 3 (good); fall through ---
660 TEXT 2,110,"HF: GOOD - high bands open"
663 GOTO 690
666 TEXT 2,110,"HF: FAIR - watch the K index"
669 GOTO 690
673 TEXT 2,110,"HF: POOR - absorbed / stormy"
690 TEXT 2,124,"live solar + geomag snapshot"
700 SHOW
710 END
800 TEXT 2,24,"No space-wx data yet."
810 TEXT 2,36,"Let CardSat fetch it,"
820 TEXT 2,48,"then run this again."
830 SHOW
840 END
900 REM === draw a bar: 150 wide from x=40 at row Y, fill P px colour L ===
910 LINE 40,Y+3,190,Y+3,8
920 IF P < 0 THEN P = 0
930 IF P > 150 THEN P = 150
940 LINE 40,Y+3,40+P,Y+3,L
950 CIRCLE 40+P,Y+3,2,L
960 RETURN
