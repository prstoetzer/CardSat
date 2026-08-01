10 REM ====== DECAY - which satellites are coming down, and how we know ======
20 REM DECAYD is days to re-entry (-1 = no estimate, 1E8 = effectively stable).
30 REM DECAYSRC says where it came from, and that matters more than the number:
40 REM   1 = the element set's MEASURED mean-motion derivative (n-dot)
50 REM   2 = modelled from B* (the drag term) - no direct measurement
60 REM A program that prints a re-entry date without saying which it used is
70 REM overstating its case, so this one always shows the source.
100 PRINT "DECAY - soonest re-entry first"
110 PRINT "idx  days     from"
120 LET B = 1E8 : REM best (smallest) days seen
130 LET C = 0   : REM how many have a usable estimate
140 FOR I = 0 TO NSAT - 1
150 SATSEL I
160 IF DECAYD < 0 THEN GOTO 260
170 IF DECAYD >= 1E8 THEN GOTO 260
180 LET C = C + 1
190 IF DECAYD > 400 THEN GOTO 260
200 LET C = C
210 IF DECAYSRC = 1 THEN PRINT I; "  "; INT(DECAYD); "     measured"
220 IF DECAYSRC = 2 THEN PRINT I; "  "; INT(DECAYD); "     modelled"
230 IF DECAYD < B THEN LET B = DECAYD
260 NEXT
270 PRINT
280 PRINT C; " of "; NSAT; " have an estimate"
290 IF B < 1E8 THEN PRINT "soonest: "; INT(B); " days"
300 IF B >= 1E8 THEN PRINT "nothing inside 400 days"
310 REM Caveats worth knowing: an object under propulsion is not doing drag
320 REM decay at all, and the modelled path depends on assumed solar activity.
330 END
