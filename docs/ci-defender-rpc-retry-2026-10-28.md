# Bounded Defender RPC retry — October 28

Hosted run 40670870773 reached the inert content suite, including transparent,
adaptive and artwork checks, then MpCmdRun failed with
`[Failed][0x800106ba] The RPC server is unavailable`. This is not a detection
verdict. The validation gate still must finish successfully before execution.

Assert-ReviewScan now permits at most three attempts, separated by two seconds,
only for exact `0x800106ba` / `0x800706ba` unavailability. It preserves every
attempt's scanner and inspection log, plus the existing aggregate log name.
The scanner's existing 180-second process bound is unchanged. It performs no
service restart, definition installation, protection-setting change, exclusion,
quarantine restoration or skip-scan fallback.

Every attempt checks that the input still exists and matches the original
SHA256 before scanning, after scanning, after threat inspection, and after any
retry delay. A missing/changed file stops immediately. Get-MpThreatDetection
is freshly queried after each completed scanner invocation, using the earliest
start time for the whole operation. A matching detection blocks even when an
RPC error also occurs. An old success receipt is removed before the new scan;
the new receipt is written only after zero scanner exit, successful fresh empty
inspection and a final unchanged-hash check.

Unknown native exits/HRESULTs remain blocked, including exit 4 or 99 with RPC
text. MpCmdRun's generic exit 2 requires its exact RPC diagnostic; direct known
RPC HRESULTs are also recognized. Inspection has a separate narrow allowance
for generic CIM/PowerShell wrapper HRESULTs (`80131500`, `80131501`, `80004005`)
only when the provider text supplies an exact allowed RPC code and no unrelated
error code. That allowance never applies to native scanner exits. A zero exit
also cannot mask an explicit unknown failure marker.

The pure policy in tools/ReviewScanPolicy.ps1 starts no I/O, process, service or
delay. TestReviewScanPolicy.ps1 passed **64 checks**, including both allowed
codes, exact code boundaries, first/last attempt behavior, unknown/mixed errors,
inspection-only wrappers, input changes, detections and recovery sequences.
No real outage was induced. A normal clean scan of the existing offscreen
GameTileLayout fixture also passed, producing an attempt log, fresh inspection
result and matching SHA256 receipt. This does not establish that every future
provider outage will recover within three attempts; exhaustion remains a
blocked validation stage, not permission to execute an unscanned fixture.
