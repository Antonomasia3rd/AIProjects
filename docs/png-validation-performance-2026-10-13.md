# PNG validation cost, 2026-10-13

Cached-content generations still validate PNG chunk CRCs. The original checksum
routine used eight polynomial steps for every byte, including on each cache-hit
restore and later native tile asset validation. The surrounding CPU-incident log
records hundreds of milliseconds in cached asset preparation; the dump itself
does not isolate the checksum's CPU share.

The routine now delegates to shared `dependencies/png_crc32.h`, using a
compile-time 256-entry lookup table for the same reflected CRC-32 polynomial.
The PNG parser still validates every chunk, header/dimensions, required IDAT/IEND
sequence, truncation, trailing bytes and expected CRC. No file validation, cache
check, failure path or renderer assertion was removed.

The portable fixture compares against the previous bit-loop implementation for
known CRC vectors, 1,000 deterministic random buffers with arbitrary initial
states and streaming partitions, and a 16 MiB buffer. **2,004 checks passed**.
On the local unoptimized MSVC build, the same 16 MiB took **550,628 microseconds**
with the reference loop and **88,870 microseconds** with the table, approximately
6.2 times faster for this routine. This is not a claim of a 6.2x whole-application
or NPSMSvc speedup. Timing is reported, not an environment-sensitive test gate.

The test is registered in portable CMake and the Windows scan-gated content
fixture wrapper. Logs/scanned binary identity are retained under ignored
`build/review-validation/PngCrc32Tests-oct13-*`. Native library unload/working-set
preferences remain unchanged; changing those defaults needs separate evidence.

A separate copied-binary, render-only cache benchmark used one deterministic
1024x576 noise BMP, all eight static assets at 100%, disabled text, and independent
temporary profiles. Both executables were scanned before execution. After one
cold run, original v27 wall times were **308/280/298 ms** and current v27 times
were **194/189/213 ms** (means 295.3 and 198.7 ms, about 33% less elapsed time in
this workload). Cold renders were 3,412 and 3,465 ms. These are whole-child wall
times, not CPU samples or a service measurement. Sequential groups can be
affected by OS/filesystem warming; three warm samples are not a general speed
guarantee.

Offline mode intentionally did not create an application log, so no per-stage
asset timing is claimed for this benchmark. The first harness stopped after one
successful cold render when it expected such a log; that control is excluded.
The corrected harness retains all eight results and fixture files under
`build/review-validation/cache-benchmark-oct13-13bb0bcb9e9e438485ca6d14a3ccb70d`.
