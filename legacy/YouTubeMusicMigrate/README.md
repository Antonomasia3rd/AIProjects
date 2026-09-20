# YouTubeMusicMigrate

Local PowerShell tooling for inspecting and tidying a YouTube Music library and
playlist layout. `youtube_music_tidy.ps1` is the main script; the cleanup script
removes its local working directory.

Browser-auth headers, tokens, cached library data, generated reports, and
personal playlist state are intentionally ignored. Create those files locally
when running the tool; do not commit or share them.
