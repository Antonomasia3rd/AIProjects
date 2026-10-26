# Shared checkout coordination

The user prefers one active checkout on `main`. Use the primary repository
checkout for ongoing work; do not create or switch to another branch/worktree
unless the user explicitly requests isolation or a concrete blocker requires it.
On the current Windows installation the primary checkout is
`D:\Users\Amiya\AppData\Local\Programs\Scripts\AIProjects`.

Multiple chats may use this directory. Recheck the branch and staged paths
immediately before committing. Coordinate overlapping files and branch changes
with the other active task. Stage only the task's own paths. Do not stash,
discard, commit or move another task's unfinished changes without authorization.

Preserve private `analysis/`, ignored builds, credentials and runtime settings.
Do not publish local artifacts as a substitute for an Actions build. Confirm
the exact workflow/source revision, tests, checksums and provenance before
describing a hosted release as successfully built or published.

Repository consolidation does not authorize native package, account, Startup,
hardware or LogonUI installation tests. Keep ordinary validation bounded and
inert unless the user has explicitly authorized the relevant integration.
