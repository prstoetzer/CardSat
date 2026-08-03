10 REM ====== DXPATH - path to a DXCC entity, with input ======
20 REM Demonstrates the PRE-RUN INPUT FORM (0.9.71). The two INPUT lines below
30 REM are not executed as prompts while the program runs: CardSat scans for
40 REM them BEFORE the run, shows one form with both fields, and the program
50 REM then executes to completion with C and N$ already set. That is why the
60 REM interpreter can still be trusted while a radio is being tuned - nothing
70 REM stops mid-run to wait for a keystroke.
80 REM
90 REM Entity codes: 291 = United States, 223 = England, 339 = Japan,
100 REM 150 = Australia, 248 = Italy, 5 = Antigua. Any current ARRL code works.
110 INPUT "DXCC code"; C
120 INPUT "Your note"; N$
130 IF POSOK = 1 THEN GOTO 160
140 PRINT "no QTH set - Settings > Location"
150 END
160 IF C > 0 THEN GOTO 190
170 PRINT "enter a DXCC entity code, e.g. 291"
180 END
190 REM DXCC$ names it; DXCCLAT/DXCCLON give its reference point.
200 LET A$ = DXCC$(C)
210 IF LEN(A$) > 0 THEN GOTO 240
220 PRINT "unknown entity code "; C
230 END
240 LET T = DXCCLAT(C)
250 LET G = DXCCLON(C)
260 REM GCDIST and GCAZ are the firmware's own great-circle routine, so these
270 REM agree with what the tracker shows rather than a second implementation.
280 LET D = GCDIST(MYLAT, MYLON, T, G)
290 LET B = GCAZ(MYLAT, MYLON, T, G)
300 PRINT "Path to "; A$
310 PRINT "  grid   "; GRID$(T, G)
320 PRINT "  bearing"; ROUND(B); " deg"
330 PRINT "  range  "; ROUND(D); " km"
340 REM Long path is the other way round the great circle.
350 LET L = B + 180
360 IF L >= 360 THEN LET L = L - 360
370 PRINT "  long   "; ROUND(L); " deg  "; ROUND(40008 - D); " km"
380 REM Free-space loss on 20 m, as a feel for the path budget.
390 PRINT "  FSPL   "; ROUND(FSPL(14.1, D)); " dB at 14.1 MHz"
400 IF LEN(N$) = 0 THEN GOTO 420
410 PRINT "note: "; N$
420 PRINT
430 PRINT "at "; TIME$; " on "; DATE$
440 END
