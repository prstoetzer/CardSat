10 REM ====== BELT - radiation environment of the selected satellite ======
20 REM Uses the real IGRF-14 field, not a dipole approximation. Two numbers
30 REM decide whether a satellite is actually IN a Van Allen belt:
40 REM   LSHELL - which field line (shell) it is on, in Earth radii
50 REM   BRATIO - how far along that shell from its magnetic equator (1 = at it)
60 REM A belt is a flux tube: its field lines come DOWN to low altitude at high
70 REM magnetic latitude, so a polar bird can sit on the L=5 shell at 184x its
80 REM equatorial field - on the belt's horn, not in the belt. That is why
90 REM altitude alone cannot answer the question.
100 IF SATOK = 1 THEN GOTO 130
110 PRINT "no satellite fix - need position, clock and live elements"
120 END
130 PRINT "BELT - shell geometry"
140 PRINT "lat "; INT(SATLAT); "  lon "; INT(SATLON); "  alt "; INT(SATALT); " km"
150 PRINT
160 PRINT "L shell  "; INT(LSHELL * 100) / 100
170 PRINT "B/B0     "; INT(BRATIO * 10) / 10
180 PRINT "|B|      "; INT(BFIELD); " nT"
190 PRINT
200 IF INBELT = 1 THEN PRINT "IN a Van Allen belt"
210 IF INBELT = 0 THEN GOSUB 400
220 IF INSAA = 1 THEN PRINT "IN the South Atlantic Anomaly"
230 REM The SAA is the inner belt reaching down into LEO where the offset field
240 REM is weak - with a real field model it falls out of the same test.
250 END
400 REM Explain the near-miss cases rather than just saying "no"
410 IF LSHELL < 1.2 THEN PRINT "below the belts (L "; INT(LSHELL * 100) / 100; ")"
420 IF LSHELL > 7 THEN PRINT "outside the outer belt (L "; INT(LSHELL * 10) / 10; ")"
430 IF BRATIO > 3 THEN PRINT "on a belt shell but "; INT(BRATIO); "x off its equator"
440 IF BRATIO > 3 THEN PRINT "  (the horn - trapped flux is lost to the atmosphere)"
450 RETURN
