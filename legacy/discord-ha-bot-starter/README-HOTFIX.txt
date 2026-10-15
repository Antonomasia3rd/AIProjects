Nifibot v0.5.2-hotfix.1 — /bot-profile server status route fix

Root cause:
Discord supports PATCH /guilds/{guild.id}/members/@me for Modify Current Member,
but GET Guild Member requires a real user snowflake. The original v0.5.2 status
handler incorrectly issued GET .../members/@me, which Discord rejects with
user_id NUMBER_TYPE_COERCE.

Fix:
- server status now GETs /guilds/{guild.id}/members/{bot_user_id}
- nickname, guild avatar, and guild banner status are still readable
- guild bio is reported as unreadable by the public GET Guild Member response;
  set/reset remain supported through PATCH Current Member
- server set/reset continue to use PATCH .../members/@me, which is the documented route

No migration.
No dependency change.
No slash-command schema change, so commands:deploy is not required.
