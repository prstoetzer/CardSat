10 REM ====== RANKPASS - rank the loaded birds by next-pass peak elevation ======
20 REM Walks the catalogue with SATSEL, and for each satellite that has a
30 REM predicted pass reads its peak elevation PASSMAX(1) and minutes-to-AOS
40 REM AOSIN. It stores (score) in the one numeric array @() and bubble-sorts
50 REM to print the best upcoming passes first. Shows off DIM @(n), array
60 REM assignment @(i)=v, and a sort - no graphics, it just PRINTs. Needs
70 REM POSOK, TIMEOK and PASSOK. One SATSEL per bird; CardSat restores your
80 REM tracked satellite when the script ends.
100 PRINT "=== Best upcoming passes ==="
110 IF POSOK = 0 THEN GOTO 900
120 IF TIMEOK = 0 THEN GOTO 900
130 LET N = NSAT
140 IF N > 40 THEN N = 40 : REM keep the sort within budget
150 REM --- two parallel arrays would be ideal but there is one @(); pack
160 REM score = peakEl*1000 + (999 - aosMin) so a higher score = better AND
170 REM sooner, and we can unpack both fields after sorting. ---
180 DIM @(40)
190 LET C = 0 : REM how many birds actually have a pass
195 IF N = 0 THEN GOTO 330
200 FOR I = 0 TO N-1
210 SATSEL I
220 IF SATOK = 0 THEN GOTO 320
230 IF PASSOK = 0 THEN GOTO 320
240 IF PASSN = 0 THEN GOTO 320
250 LET E = PASSMAX(1) : REM peak elevation of the next pass
260 LET T = AOSIN : REM minutes to AOS (may be negative = up now)
270 IF T < 0 THEN T = 0
280 IF T > 999 THEN T = 999
290 REM pack: elevation dominates, sooner passes break ties
300 LET @(C) = INT(E) * 1000 + (999 - INT(T))
310 LET C = C + 1
320 NEXT I
330 IF C = 0 THEN GOTO 900
340 REM --- bubble sort descending ---
350 FOR I = 0 TO C-2
360 FOR J = 0 TO C-2-I
370 IF @(J) >= @(J+1) THEN GOTO 420
380 LET T = @(J)
390 LET @(J) = @(J+1)
400 LET @(J+1) = T
410 REM (swapped)
420 NEXT J
430 NEXT I
440 REM --- print the top 8 unpacked ---
450 LET M = C
460 IF M > 8 THEN M = 8
470 PRINT "rank  peakEl  in(min)"
480 FOR I = 0 TO M-1
490 LET S = @(I)
500 LET E = INT(S / 1000)
510 LET T = 999 - (S - E * 1000)
520 PRINT " "; I+1; "     "; E; "      "; T
530 NEXT I
540 PRINT "---------------------------"
550 PRINT C; " birds have a pass."
560 END
900 PRINT "Not enough data yet."
910 PRINT "Load TLEs, set location/clock,"
920 PRINT "let passes predict, run again."
930 END
