$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Start-Process -FilePath (Join-Path $root 'NowPlayingTile.exe') -ArgumentList '--widget', '--allow-multiple'
