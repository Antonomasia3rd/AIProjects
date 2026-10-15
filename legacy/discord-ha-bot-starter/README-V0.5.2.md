# Nifibot v0.5.2 — bot profile customization

Adds `/bot-profile` for both Discord per-server bot profiles and the global bot account profile.

## Commands

### Server-specific profile

- `/bot-profile server status`
- `/bot-profile server set nickname:<text> bio:<text> avatar:<attachment> banner:<attachment> reason:<text>`
- `/bot-profile server reset field:<nickname|avatar|banner|bio> reason:<text>`

All fields on `server set` are optional; at least one must be supplied. Server avatar, banner, bio and nickname persist at Discord and only affect the current server.

Changing the server nickname requires Nifibot itself to have the Discord **Change Nickname** permission. The other current-member profile fields are handled by Discord's current-guild-member profile API.

### Global bot profile

- `/bot-profile global status`
- `/bot-profile global set username:<text> avatar:<attachment> banner:<attachment> confirm:<bool>`
- `/bot-profile global reset field:<avatar|banner>`

Global changes affect the bot account everywhere. A username change requires `confirm:true` because Discord rate-limits/restricts bot renames. Global profile changes are accepted only from the configured `DISCORD_GUILD_ID` home server.

## Access

The command defaults to Discord's **Manage Server** (`ManageGuild`) permission.

## Images

PNG, JPEG, GIF and WebP attachments up to 10 MiB are accepted. The bot downloads the Discord attachment, converts it to an image data URI, and sends it to Discord's profile API.

## Deployment

No database migration is required. Slash commands must be redeployed; Nifibot's `/cluster-update` staging process already runs `commands:deploy` automatically.
