Nifibot v0.5.2-hotfix.2 — voice DB pool resilience

Observed failure on A1504VA:
- main cluster lease renewal lost its PostgreSQL connection and correctly failed safe
- the main db Pool also logged an idle ECONNRESET and survived
- the independent voice HA Pool received the same pooler/network reset
- voiceDb had no Pool-level `error` listener, so Node treated it as an uncaught exception
- Nifibot shut down and remained offline because no watchdog exists yet

Fix:
- add a Pool `error` listener to voiceDb mirroring src/db.ts
- broken idle clients are logged and discarded by pg-pool instead of killing Nifibot

No migration, dependency, or slash-command schema changes.
The staged self-updater build is the authoritative TypeScript validation.
