10 REM ====== CALLPARSE - text functions on a callsign ======
20 REM Demonstrates the string variables and text functions added in 0.9.71:
30 REM LEFT$ RIGHT$ MID$ LEN INSTR VAL UCASE$ TRIM$ and string comparison.
40 REM Index rules follow Microsoft BASIC exactly, which matters when porting:
50 REM   MID$(s, start, len)  - START IS 1-BASED, not 0
60 REM   INSTR(hay, needle)   - returns a 1-BASED position, 0 means not found
70 INPUT "Callsign"; C$
80 LET C$ = UCASE$(TRIM$(C$))
90 IF LEN(C$) > 2 THEN GOTO 120
100 PRINT "enter a callsign, e.g. N8HM or G0RYT/P"
110 END
120 PRINT "callsign: "; C$; "  ("; LEN(C$); " chars)"
130 REM --- portable/mobile suffix -------------------------------------------
140 LET S = INSTR(C$, "/")
150 IF S = 0 THEN GOTO 200
160 LET P$ = MID$(C$, S + 1)
170 LET C$ = LEFT$(C$, S - 1)
180 PRINT "suffix:   /"; P$
190 IF P$ = "P" THEN PRINT "  portable"
200 REM --- find the digit that separates prefix from suffix ------------------
210 LET D = 0
220 FOR I = 1 TO LEN(C$)
230 LET H$ = MID$(C$, I, 1)
240 IF H$ >= "0" THEN IF H$ <= "9" THEN LET D = I
250 IF D > 0 THEN GOTO 270
260 NEXT I
270 IF D > 0 THEN GOTO 300
280 PRINT "no digit - not a standard callsign"
290 END
300 LET X$ = LEFT$(C$, D)
310 LET Y$ = MID$(C$, D + 1)
320 PRINT "prefix:   "; X$
330 PRINT "suffix:   "; Y$
340 REM --- a few things the prefix tells you --------------------------------
350 LET F$ = LEFT$(C$, 1)
360 IF F$ = "K" THEN GOTO 400
370 IF F$ = "N" THEN GOTO 400
380 IF F$ = "W" THEN GOTO 400
390 GOTO 420
400 PRINT "looks like a US callsign (call area "; VAL(MID$(C$, D, 1)); ")"
410 END
420 PRINT "not a US prefix - see the DXCC lookup in Tools"
430 END
