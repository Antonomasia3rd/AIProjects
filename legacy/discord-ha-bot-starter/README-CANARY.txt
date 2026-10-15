Nifibot v0.5.1.6a self-update canary

This corrects the malformed TypeScript file in the previous v0.5.1.6 canary.
The updater itself reached the staging build step correctly; the staged build
failed because the test file was syntactically invalid.

This overlay only adds:
  src/selfUpdateCanary.ts

No migration.
No dependency changes.
No slash-command changes.
