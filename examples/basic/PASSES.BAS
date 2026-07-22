10 REM ====== PASSES - upcoming passes for the selected satellite ======
20 REM Prints a table of the next passes CardSat has predicted for the
30 REM ACTIVE satellite: minutes to AOS, minutes to LOS, the pass length,
40 REM and the maximum elevation, with a tiny bar so the good passes stand
50 REM out. Uses PASSAOS(k)/PASSLOS(k)/PASSMAX(k), k = 1..PASSN. No graphics,
60 REM no INPUT - it just prints. Select a satellite on the Track screen
70 REM first; if none is predicted, PASSN is 0 and it says so.
100 PRINT "=== Upcoming passes ==="
110 IF PASSOK = 0 THEN GOTO 400
120 LET P = PASSN
130 IF P = 0 THEN GOTO 400
140 IF P > 8 THEN P = 8
150 PRINT "  # AOSmin LOSmin  len maxEl"
160 FOR K = 1 TO P
170 LET A = PASSAOS(K)
180 LET L = PASSLOS(K)
190 LET M = PASSMAX(K)
200 LET D = L - A
210 GOSUB 600
220 NEXT K
230 PRINT "-----------------------------"
240 PRINT "Bar: each * ~ 10 deg of max el"
250 END
400 PRINT "No pass predicted."
410 PRINT "Pick a satellite on Track,"
420 PRINT "then run this again."
430 END
600 REM --- print one row: K A L M D, then a max-el bar ---
610 PRINT " ";
620 IF K < 10 THEN PRINT " ";
630 PRINT K; " ";
640 GOSUB 700
650 REM print elevation bar: one * per 10 degrees
660 LET N = INT(M / 10)
670 FOR J = 1 TO N
680 PRINT "*";
690 NEXT J
695 PRINT ""
696 RETURN
700 REM --- print A L D M as padded-ish integers ---
710 PRINT INT(A); " ";
720 PRINT INT(L); " ";
730 PRINT INT(D); " ";
740 PRINT INT(M); "  ";
750 RETURN
