# Baseline test isolation

Updated 2026-09-11.

The native and managed shared runtime suites still created real sign-in shortcuts by default, independently of the hardened RepoTools smoke. Their normal cleanup could be bypassed by process termination. This contradicted the user's interruption-safety requirement.

Default tests now retain injected transaction/rollback coverage and native ShellLink serialization/concurrency tests in temporary directories, while skipping the final real Startup-folder integration block. That block requires explicit `--allow-startup-integration` on either runner. It has not been invoked during this fix. The native helper rejects unexpected arguments before testing; the managed helper also requires an existing repository directory, so stale test-shortcut arguments cannot accidentally launch the full test suite at sign-in.

Validation: default `tools/TestSharedBaseline.cmd` passed native runtime tests and301 source checks; default `tools/TestLegacyUtilities.cmd` passed. Both logs explicitly report the real Startup integration skip. Temporary-directory ShellLinks do not act as sign-in entries. No actual Startup shortcut was created by these default validation runs.

The opt-in is for a disposable Windows test environment: interruption can still leave a real test shortcut. It is deliberately excluded from default and CI runs. Tests are not a reason to rely on continued agent execution for cleanup.
