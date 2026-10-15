# Nifibot Logging v0.3 — Activity + Verbose Context Patch

Overlay this patch on top of the current v0.2.9 project.

## What changes

### More verbose message logs
Deleted messages now include:
- author
- channel + channel link
- original message URL
- when the message was originally sent
- how old it was when deleted
- last edit time when known
- deletion-observed time
- reply target link when present
- richer attachment/embed/sticker context
- clearer audit-log attribution when available

Edited messages now include:
- original send time
- channel link
- message jump link
- existing before/after text and rich embed summaries

Reaction logs now include the original message send time.

### Member / moderation logging
Adds richer:
- member joins
- member leaves
- kick detection + audit actor
- nickname changes
- role changes
- timeout add/change/remove
- bans
- unbans

### Voice logging
Adds:
- VC join
- VC leave
- VC move
- self mute/deafen
- server mute/deafen
- stream start/stop
- camera start/stop
- best-effort audit attribution for forced moves/server moderation

### New routing categories
`/logging channel` gains:
- `voice`
- `moderation`

Existing fallback behavior:
- voice -> members -> messages
- moderation -> members -> messages

## Apply

1. Stop all Nifibot processes.
2. Overlay the patch into the project root.
3. Run:

```cmd
npm run build
npm run db:migrate
npm run commands:deploy
npm start
```

Migration 003 only adds two nullable logging-destination columns.

## Suggested channel setup

```text
/logging channel kind:messages channel:#nifibot・logs
/logging channel kind:members channel:#server・logs
/logging channel kind:voice channel:#voice・logs
/logging channel kind:moderation channel:#alerts
```

You can point any category back to the same log channel if preferred.

## Notes

A deleted-message "Original message URL" is still useful as a permanent reference,
but Discord generally cannot jump to a message after the message itself has been deleted.

Voice move attribution is explicitly best-effort because Discord's gateway event does
not reliably identify who caused a forced move.
