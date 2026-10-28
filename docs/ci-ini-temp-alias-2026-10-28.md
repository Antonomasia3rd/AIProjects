# Hosted INI fixture: short temporary-directory alias

Hosted [diagnostic run 40658601359](https://github.com/Antonomasia3rd/AIProjects/actions/runs/40658601359)
identified the exact exit-2 cause: the managed fixture's parent used
`C:\USERS\RUNNERADMIN\APPDATA\LOCAL\TEMP`, while native GetTempPathW returned
`C:\USERS\RUNNER~1\APPDATA\LOCAL\TEMP`. They are two spellings of the same
directory. The earlier lexical guard rejected the first --probe, before the
mutex/interoperability checks could run.

The failure was reproduced locally by giving the child processes an 8.3 TMP/TEMP
alias. All name/hash checks passed and the first lock probe failed, matching
the hosted call path. This is a fixture admission defect, not evidence that
the application's INI locking failed.

The native worker now compares existing temp-directory handles by volume and
file identity. It still admits only a direct child of the trusted native temp
directory, and now requires an ordinary directory with the exact fixture prefix
and 32 hexadecimal GUID characters. Reparse-point fixture roots, wrong parents,
nested roots, missing roots and invalid GUID leaves remain rejected. The
production file-identity/hash and lock implementations are unchanged.

Managed regression coverage includes the short/long alias, the same valid
fixture under a different trusted-temp context, nested/outside targets, malformed
GUID leaves and unchanged data. The original lock exclusion, recursive lock,
rollback, comments and 100 writes per language remain tested. Final local run:
**27 checks passed**, with both new workers scanned clean before execution.
The workers retain their existing deadlines; no account, Startup, package or
hardware integration was exercised. Hosted acceptance is recorded after the
corresponding corrected run completes; local success is not a hosted release.
