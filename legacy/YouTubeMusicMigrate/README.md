# YouTubeMusicMigrate

Local PowerShell tooling for inspecting and tidying a YouTube Music library and
playlist layout. `youtube_music_tidy.ps1` is the main script. The cleanup script
previews archiving selected legacy state and top-level reports; `-Apply` moves
those selected files into timestamped subdirectories. It does not remove the
working directory, authentication headers or active caches.

Browser-auth headers, tokens, cached library data, generated reports, and
personal playlist state are intentionally ignored. Create those files locally
when running the tool; do not commit or share them.
