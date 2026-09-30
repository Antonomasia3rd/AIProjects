# ChromeProfileCounter product overlay. The implementation lives in
# dependencies/ChromeProfileCounter so this legacy entry point remains a thin
# compatibility launcher.
$implementation = Join-Path $PSScriptRoot "..\..\dependencies\ChromeProfileCounter\chrome_profile_counter_app.ps1"
& $implementation @args
