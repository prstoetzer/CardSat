10 REM ====== DOPPLER - transponder shift for the active satellite ======
20 REM Snapshots transponder 0 of the ACTIVE satellite with TXSEL, then uses
30 REM the current range-rate SATRR (km/s, + = receding) to work out the
40 REM Doppler-corrected downlink you HEAR and the uplink you must SEND:
50 REM   heard = TXDL * (1 - rr/c)      send = TXUL * (1 + rr/c)
60 REM c = 299792.458 km/s. Prints the numbers in kHz and draws a shift bar
70 REM (green = toward you / blue-shift, red = away). Pick a satellite with a
80 REM linear/FM transponder on Track first.
100 LET V = 299792.458 : REM speed of light, km/s
110 CLS
120 TEXT 2,2,"DOPPLER  tx0"
130 IF SATOK = 0 THEN GOTO 800
140 TXSEL 0
150 LET R = SATRR : REM range rate km/s (+ receding)
160 LET F = R / V : REM fractional shift
170 REM --- downlink you hear ---
180 LET D = TXDL
190 LET E = D - D * F : REM received (Hz)
200 REM --- uplink you must transmit ---
210 LET U = TXUL
220 LET S = U + U * F : REM transmit (Hz)
230 REM --- print in kHz (÷1000) ---
240 TEXT 2,20,"DL nom kHz"
250 TEXT 92,20,INT(D / 1000)
260 TEXT 2,32,"DL hear kHz"
270 TEXT 92,32,INT(E / 1000)
280 TEXT 2,48,"UL nom kHz"
290 TEXT 92,48,INT(U / 1000)
300 TEXT 2,60,"UL send kHz"
310 TEXT 92,60,INT(S / 1000)
320 REM --- shift in Hz on the downlink ---
330 LET G = D * F : REM downlink shift magnitude-ish (Hz)
340 TEXT 2,78,"DL shift Hz"
350 LET W = 0 - G
360 TEXT 92,78,INT(W)
370 REM --- a centre-zero bar: right = blue (approaching), left = red ---
380 LINE 20,110,220,110,8
390 LINE 120,104,120,116,1
400 REM scale: clamp shift to +-100 across 100px each side
410 LET B = W / 1000 * 100 : REM Hz -> px (1px per 10 Hz)
420 IF B > 100 THEN B = 100
430 IF B < 0 - 100 THEN B = 0 - 100
440 LET X = 120 + B
450 LET L = 4
460 IF W >= 0 THEN L = 3
470 LINE 120,110,X,110,L
480 CIRCLE X,110,2,L
490 TEXT 2,122,"grn=toward you  red=away"
500 SHOW
510 END
800 TEXT 2,20,"No active satellite."
810 TEXT 2,32,"Pick one on Track."
820 SHOW
830 END
