10 REM ====== SIEVE - primes by the Sieve of Eratosthenes ======
20 REM A pure-maths classic: allocate the one numeric array @() as a flag
30 REM table for 2..N, then strike out every multiple of each prime. What
40 REM survives is prime. Demonstrates DIM @(n), @(i) as both lvalue and
50 REM rvalue, nested FOR, and the MOD operator. No system data, so it
60 REM always runs. Prints the primes across the console. N<=256 (the array
70 REM cap); 120 keeps it quick.
100 LET N = 120
110 DIM @(N+1) : REM array needs N+1 slots: indices 0..N are all used below
120 REM --- mark everything "prime" (1) to start; 0 and 1 are not primes ---
130 FOR I = 0 TO N
140 LET @(I) = 1
150 NEXT I
160 LET @(0) = 0
170 LET @(1) = 0
180 REM --- strike multiples: for each i still standing, clear 2i,3i,... ---
190 FOR I = 2 TO N
200 IF @(I) = 0 THEN GOTO 260
210 REM i is prime; clear its multiples
220 LET J = I + I
230 IF J > N THEN GOTO 260
240 LET @(J) = 0
250 LET J = J + I
255 GOTO 230
260 NEXT I
270 REM --- print survivors, 10 per line ---
280 PRINT "Primes up to "; N; ":"
290 LET C = 0
300 FOR I = 2 TO N
310 IF @(I) = 0 THEN GOTO 360
320 PRINT I; " ";
330 LET C = C + 1
340 IF C MOD 10 <> 0 THEN GOTO 360
350 PRINT ""
360 NEXT I
370 PRINT ""
380 PRINT "Count: "; C
390 END
