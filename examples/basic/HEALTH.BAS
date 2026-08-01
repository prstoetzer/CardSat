10 REM ====== HEALTH - will this device finish the pass? ======
20 REM The numbers that actually decide it. Note HEAPBLK, not just HEAPFREE:
30 REM a big allocation needs a CONTIGUOUS block, so total free heap can look
40 REM healthy while nothing large will fit. On this no-PSRAM board that is
50 REM what stops a TLS upload, not the total.
100 CLS
110 TEXT 2,2,"STATION HEALTH"
120 LET Y = 20
130 TEXT 4,Y,"battery"
140 TEXT 90,Y,INT(BATT)
150 TEXT 120,Y,"%"
160 TEXT 150,Y,INT(BATTMV)
170 TEXT 195,Y,"mV"
180 LET Y = Y + 12
190 IF CHARGING = 1 THEN TEXT 4,Y,"charging (inferred)"
200 IF CHARGING = 0 THEN TEXT 4,Y,"on battery"
210 LET Y = Y + 14
220 TEXT 4,Y,"heap free"
230 TEXT 90,Y,INT(HEAPFREE / 1024)
240 TEXT 130,Y,"KB"
250 LET Y = Y + 12
260 TEXT 4,Y,"largest blk"
270 TEXT 90,Y,INT(HEAPBLK / 1024)
280 TEXT 130,Y,"KB"
290 REM Under ~8 KB contiguous, a TLS upload will not get its buffers.
300 IF HEAPBLK < 8192 THEN TEXT 150,Y,"LOW"
310 LET Y = Y + 14
320 TEXT 4,Y,"uptime min"
330 TEXT 90,Y,INT(UPTIME / 60)
340 LET Y = Y + 12
350 TEXT 4,Y,"element age d"
360 TEXT 110,Y,INT(GPAGE * 10) / 10
370 REM Elements older than a few days degrade prediction accuracy badly.
380 IF GPAGE > 3 THEN TEXT 150,Y,"STALE"
390 SHOW
400 END
