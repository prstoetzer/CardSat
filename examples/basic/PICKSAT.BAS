10 REM ====== PICKSAT - choose a satellite from inside BASIC ======
20 REM Nothing needs selecting before you run this. SATSEL i picks any bird in
30 REM the loaded catalog (i = 0..NSAT-1) and brings its TRANSPONDERS with it,
40 REM so NTX is that satellite's count and TXSEL k reads ITS transponder k.
50 REM We list every satellite that is UP now and has a transponder.
60 REM SATOK=0 means "valid index, but no fix / dead elements" - skip, don't stop.
100 PRINT "PICKSAT - up now, with a transponder"
110 PRINT "idx  el   rr km/s  downlink MHz"
120 LET N = 0
130 FOR I = 0 TO NSAT - 1
140 SATSEL I
150 IF SATOK = 0 THEN GOTO 260
160 IF SATEL <= 0 THEN GOTO 260
170 IF NTX <= 0 THEN GOTO 260
180 TXSEL 0
190 IF TXOK = 0 THEN GOTO 260
200 LET N = N + 1
210 PRINT I; "  "; INT(SATEL); "  "; INT(SATRR * 10) / 10; "  "; INT(TXDL / 1000) / 1000
260 NEXT
270 PRINT
280 IF N = 0 THEN PRINT "none up with a transponder right now"
290 IF N > 0 THEN PRINT N; " candidate(s). SATSEL that idx to work it."
300 REM Tip: in immediate mode (Fn+i) you can try one line at a time, e.g.
310 REM   SATSEL 12
320 REM   PRINT SATEL, NTX
330 END
