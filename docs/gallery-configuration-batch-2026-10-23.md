# Gallery configuration staging, 23 October 2026

The gallery lifecycle fixture had previously exceeded its unchanged 45-second
watchdog without reporting which operation was slow. Added phase diagnostics
showed its long transaction/recovery group repeatedly serializing 28 or 29
gallery rows. Every row now owns dozens of delivery/cache/asset policy keys.
The old loop split, scanned and rebuilt the entire growing INI for every key,
including when creating recovery records. This was a real setup/resize cost as
well as a fixture cost.

`dependencies/gallery_ini_batch.h` now applies a complete assignment vector in
two document passes. It validates every assignment before mutation, preserves
unrelated text lines/comments, updates every duplicate key in duplicate
sections, uses case-insensitive names, and inserts missing keys in the same
first section as the existing writer. It also preserves that writer's precise
BOM, quoting, CRLF and blank-line placement semantics, including keys inserted
after a later section has already been added. Failed batches retain the exact
input text. This helper is used only for gallery settings staging and recovery
configuration encoding; atomic file publication, validation, concurrency guards,
rollback, allocation history and queues keep their existing behavior.

Validation used private temporary profiles and injected clear/pin operations.
No real gallery, account, notification, package, pin or resident was changed.

| Fixture | Result |
|---|---|
| `GalleryIniBatchTests`, `/W4 /WX /O2` | 4,194 passed |
| Same optimized `ContentGalleryLifecycleHostTests` | 295 passed before and after |
| Whole gallery lifecycle fixture | 17.159 seconds before; 3.806 seconds after |
| Transaction/recovery/resize phase within it | 14.406 seconds before; 1.016 seconds after |
| 28 tiles × 70 synthetic assignments, exact byte equivalence | Reference 2,527,864 microseconds; batch 15,776 microseconds |

The serialization comparison overlapped compilation and is a diagnostic, not a
controlled system benchmark. The before lifecycle run was quiet; the after run
may have overlapped the next product compiler. The internal 45-second watchdog
and assertions were not increased or weakened. Both fixture executables passed
Defender scans before execution, and the after fixture's hash was checked
against its scan record.

Evidence:

- `build/review-validation/gallery-baseline-diagnostics-oct23.log`
- `build/review-validation/gallery-batch-lifecycle-oct23.log`
- `build/review-validation/gallery-ini-batch-benchmark-oct23.log`

The prior failed build command also exposed a validation orchestration issue:
editing a `.cmd` file while `cmd.exe` is reading it can shift its current read
offset and execute a fragment of a line after compilation. Build scripts must
stay frozen during their execution or be copied to a private, path-correct
wrapper first. In that instance the executable had linked; it was scanned and
validated separately before use.
